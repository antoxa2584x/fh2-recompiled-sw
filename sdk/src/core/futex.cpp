/**
 * @file        core/futex.cpp
 * @brief       FutexWait / FutexWakeAll for each platform (rex/thread/futex.h)
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/platform.h>
#include <rex/thread.h>
#include <rex/thread/futex.h>

#include <chrono>
#include <climits>

// Switch first: platform.h also turns on REX_PLATFORM_LINUX there, but Horizon has no futex system call.
#if REX_PLATFORM_SWITCH
extern "C" {
#include <switch/types.h>
#include <switch/kernel/svc.h>
}
#elif defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace rex::thread {

void FutexWait(std::atomic<uint32_t>& word, uint32_t expected, int64_t nanoseconds) {
#if REX_PLATFORM_SWITCH
  // Returns at once with KernelError_InvalidState when the word no longer holds 'expected', and with
  // KernelError_TimedOut after the timeout: either way the caller re-checks.
  svcWaitForAddress(reinterpret_cast<void*>(&word), ArbitrationType_WaitIfEqual, s64(expected), nanoseconds);
#elif defined(__linux__)
  timespec limit{time_t(nanoseconds / 1000000000), long(nanoseconds % 1000000000)};
  syscall(SYS_futex, reinterpret_cast<uint32_t*>(&word), FUTEX_WAIT_PRIVATE, expected, &limit, nullptr, 0);
#else
  (void)word;
  (void)expected;
  Sleep(std::chrono::nanoseconds(nanoseconds));
#endif
}

void FutexWakeAll(std::atomic<uint32_t>& word) {
#if REX_PLATFORM_SWITCH
  // A count of -1 wakes every waiter.
  svcSignalToAddress(reinterpret_cast<void*>(&word), SignalType_Signal, 0, -1);
#elif defined(__linux__)
  syscall(SYS_futex, reinterpret_cast<uint32_t*>(&word), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
#else
  (void)word;
#endif
}

}  // namespace rex::thread
