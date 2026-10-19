// fh2 - profile dump of the instrumented Switch build, for profile-guided optimization (PGO), adapted from
// nfsmw-nx (app/src/nfsmw_pgo.cpp).
//
// Only does something with FH2_SWITCH_PGO=generar (app/CMakeLists.txt). GCC names each object's profile
// <FH2_PGO_DIR>/<object path relative to the build folder, '/' turned into '#'>.gcda, a path on the PC that builds.
// libgcov relocates it with GCOV_PREFIX / GCOV_PREFIX_STRIP, read at every dump: the PC folder's components are
// stripped and sdmc:/switch/fh2/pgo put in front.
//
// The Switch does not always close the program cleanly (the HOME menu kills it), so the profile cannot wait for the
// exit: a thread dumps it every 2 minutes and resets the counters. libgcov adds to what each .gcda already holds, so
// the dumps of one or several runs add up. The files are then copied to FH2_PGO_DIR on the PC for the
// FH2_SWITCH_PGO=usar build (docs/switch.md).
#if defined(FH2_PGO_GERAR) && defined(__SWITCH__)

#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

extern "C" {
void __gcov_dump(void);
void __gcov_reset(void);
}

namespace {

constexpr const char* kPastaSd = "sdmc:/switch/fh2/pgo";
Thread g_hilo_pgo;

void HiloPgo(void*) {
  for (int despejo = 1;; ++despejo) {
    svcSleepThread(120LL * 1000 * 1000 * 1000);
    mkdir(kPastaSd, 0777);
    const u64 antes = armGetSystemTick();
    __gcov_dump();
    __gcov_reset();
    const double ms = double(armTicksToNs(armGetSystemTick() - antes)) / 1e6;
    if (FILE* f = std::fopen("sdmc:/switch/fh2/pgo/despejos.txt", "a")) {
      std::fprintf(f, "despejo %d: %.0f ms\n", despejo, ms);
      std::fclose(f);
    }
  }
}

__attribute__((constructor)) void IniciarPgo() {
  // FH2_PGO_DIR is an absolute path: strip all its components.
  int componentes = 0;
  for (const char* c = FH2_PGO_DIR; *c; ++c) {
    componentes += *c == '/' && c[1] != '/' && c[1] != '\0';
  }
  setenv("GCOV_PREFIX", kPastaSd, 1);
  setenv("GCOV_PREFIX_STRIP", std::to_string(componentes).c_str(), 1);
  // The game's normal priority (0x3B) on any core: it only works once every 2 minutes.
  if (R_SUCCEEDED(threadCreate(&g_hilo_pgo, HiloPgo, nullptr, nullptr, 64 * 1024, 0x3B, -2))) {
    threadStart(&g_hilo_pgo);
  }
}

}  // namespace

#endif
