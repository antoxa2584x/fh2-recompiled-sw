/**
 * @file        rex/thread/futex.h
 * @brief       Wait on a 32-bit word until another thread changes it (futex)
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace rex::thread {

// Sleeps while 'word' still holds 'expected', at most 'nanoseconds'. It may return early (spuriously or because
// the word changed); callers re-check what they wait for. Linux and Android: the futex system call. Switch: the
// kernel's address arbiter (svcWaitForAddress). Elsewhere: a plain sleep for the whole time.
void FutexWait(std::atomic<uint32_t>& word, uint32_t expected, int64_t nanoseconds);

// Wakes every thread sleeping in FutexWait on 'word'.
void FutexWakeAll(std::atomic<uint32_t>& word);

}  // namespace rex::thread
