// fh2 - CPU, GPU and memory clocks on the Switch ("relógios"), ported from the author's FlatOut 2 port
// (flatout2-xbox, src/switch_nx.c: clk_apply / clk_keeper, FO2_DOCK_BOOST).
//
// The boost: the docked rates, CPU 1785 / GPU 768 / memory 1600 MHz, in both modes. The docked profile already
// runs the GPU at 768 and the memory at 1600, so docked it adds the CPU (1020 -> 1785 MHz); handheld it also takes
// the GPU from 307.2 and the memory from 1331.2. The game is far below full speed on both walls (the emulated GPU's
// command processor and the draws), so it is on by default: more heat and battery, fh2_switch_boost=false turns it
// off, fh2_switch_boost_docked_only=true keeps handheld at the system's clocks.
//
// fh2_switch_cpu_mhz / _gpu_mhz / _mem_mhz choose other rates (the closest the hardware lists, never above the
// request; capped at 1785 / 921.6 / 1600 MHz). Through clkrst (8.0.0+) or pcv, as sys-clk does; the old rates
// come back on exit.
//
// The system resets the clocks on dock/undock, after sleep and when the game comes back from HOME, so a thread
// applies them again every second while the game has focus, and every 250 ms for 5 s after a focus, dock or
// performance-mode change (applet hook). It runs at a high priority: in FlatOut 2, at the lowest one the busy
// guest threads on its core starved it and the clocks stayed at the system's after HOME.
//
// Clock managers (sys-clk "sys:clk", Horizon OC "hoc:clk") set clocks themselves, and fighting them every second
// froze consoles (nfsu2-sw issue #25). So: when one is running nothing is touched (its profile rules;
// fh2_switch_clock_force=true overrides); a rate already at or above the request is left alone (never lower
// someone else's overclock); and a module that is put back more than 5 times in 30 s with no dock/focus/sleep
// change in between is given up to whoever does it.

#include <switch.h>

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "fh2_switch_relojes.h"

REXCVAR_DEFINE_BOOL(fh2_switch_boost, true, "FH2",
                    "Switch: run the CPU, GPU and memory at the docked rates (1785 / 768 / 1600 MHz) in both "
                    "modes (app/src/fh2_switch_relojes.cpp)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fh2_switch_boost_docked_only, false, "FH2",
                    "Switch: apply fh2_switch_boost while docked only; handheld keeps the system's clocks")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(fh2_switch_cpu_mhz, 0, "FH2", "Switch: CPU clock in MHz (0 = fh2_switch_boost's, or the system's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(fh2_switch_gpu_mhz, 0, "FH2", "Switch: GPU clock in MHz (0 = fh2_switch_boost's, or the system's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(fh2_switch_mem_mhz, 0, "FH2",
                     "Switch: memory clock in MHz (0 = fh2_switch_boost's, or the system's)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(fh2_switch_clock_force, false, "FH2",
                    "Switch: set the clocks even when sys-clk or Horizon OC is running")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace fh2::relojes {
namespace {

struct Modulo {
  const char* nome;
  PcvModule mod;
  PcvModuleId id;
  uint32_t teto_hz, boost_hz;
  uint32_t quer_hz = 0, antigo_hz = 0;
  std::atomic<uint32_t> posto_hz{0};  // what we set and keep; 0 = given up or never set
  bool aberto = false;
  int brigas = 0;
  uint64_t briga_t0 = 0;
  ClkrstSession sessao{};
};

Modulo g_mod[3] = {
    {"CPU", PcvModule_CpuBus, PcvModuleId_CpuBus, 1785000000u, 1785000000u},
    {"GPU", PcvModule_GPU, PcvModuleId_GPU, 921600000u, 768000000u},
    {"memory", PcvModule_EMC, PcvModuleId_EMC, 1600000000u, 1600000000u},
};
int g_clkrst = -1;  // 1 clkrst, 0 pcv, -1 not up
bool g_so_docked = false;
std::atomic<bool> g_parar{false};
Thread g_thread;
bool g_thread_up = false;
UEvent g_acordar;
AppletHookCookie g_hook;

Result Ler(Modulo& m, uint32_t* hz) {
  return g_clkrst ? clkrstGetClockRate(&m.sessao, hz) : pcvGetClockRate(m.mod, hz);
}

Result Por(Modulo& m, uint32_t hz) {
  return g_clkrst ? clkrstSetClockRate(&m.sessao, hz) : pcvSetClockRate(m.mod, hz);
}

// The highest listed rate not above 'quer' ('quer' itself when there is no list).
uint32_t Escolher(Modulo& m, uint32_t quer) {
  uint32_t taxas[64], melhor = 0;
  s32 n = 0;
  PcvClockRatesListType tipo;
  const Result rc = g_clkrst ? clkrstGetPossibleClockRates(&m.sessao, taxas, 64, &tipo, &n)
                             : pcvGetPossibleClockRates(m.mod, taxas, 64, &tipo, &n);
  if (R_FAILED(rc) || n <= 0 || tipo != PcvClockRatesListType_Discrete) {
    return quer;
  }
  for (s32 k = 0; k < n; ++k) {
    if (taxas[k] <= quer && taxas[k] > melhor) {
      melhor = taxas[k];
    }
  }
  return melhor ? melhor : quer;
}

// Is another process serving this name? (sys-clk's own check: registering it fails with "already registered".)
bool ServicoRodando(const char* nome) {
  Handle h;
  const Result rc = smRegisterService(&h, smEncodeName(nome), false, 1);
  if (R_SUCCEEDED(rc)) {
    smUnregisterService(smEncodeName(nome));
    svcCloseHandle(h);
    return false;
  }
  return rc == MAKERESULT(21, 4);  // sm: AlreadyRegistered
}

bool Docked() { return appletGetOperationMode() == AppletOperationMode_Console; }

// Runs on the thread that pumps applet messages (windowed_app_context_switch.cpp's loop).
void Gancho(AppletHookType hook, void*) {
  if (hook == AppletHookType_OnFocusState || hook == AppletHookType_OnOperationMode ||
      hook == AppletHookType_OnPerformanceMode || hook == AppletHookType_OnResume) {
    ueventSignal(&g_acordar);
  }
}

void Guardiao(void*) {
  unsigned avisos = 0, rapido = 0;
  int modo = -1;
  const uint64_t freq = armGetSystemTickFreq();
  while (!g_parar.load(std::memory_order_acquire)) {
    if (R_SUCCEEDED(waitSingle(waiterForUEvent(&g_acordar), rapido ? 250000000ull : 1000000000ull))) {
      rapido = 20;
    } else if (rapido) {
      --rapido;
    }
    if (g_parar.load(std::memory_order_acquire) || appletGetFocusState() != AppletFocusState_InFocus) {
      continue;
    }
    const bool docked = Docked();
    if (int(docked) != modo) {
      modo = docked;
      REXLOG_INFO("[relogios] {}: {}", docked ? "docked" : "handheld",
                  g_so_docked && !docked ? "system clocks" : "clocks boosted");
    }
    if (g_so_docked && !docked) {
      continue;  // handheld: the system's clocks (it resets them itself on undock)
    }
    for (Modulo& m : g_mod) {
      const uint32_t posto = m.posto_hz.load(std::memory_order_relaxed);
      uint32_t hz = 0;
      if (!posto || R_FAILED(Ler(m, &hz)) || hz == posto) {
        continue;
      }
      if (hz > posto) {
        REXLOG_INFO("[relogios] {} raised to {} MHz by someone else: left to them", m.nome, hz / 1000000u);
        m.posto_hz.store(0, std::memory_order_relaxed);
        continue;
      }
      // The system's own resets come with an applet event (rapido > 0); a reset without one is another
      // program setting clocks.
      const uint64_t agora = armGetSystemTick();
      if (!rapido) {
        if (!m.brigas || agora - m.briga_t0 > 30 * freq) {
          m.brigas = 0;
          m.briga_t0 = agora;
        }
        if (++m.brigas > 5) {
          REXLOG_WARN("[relogios] {} keeps going back to {} MHz: another clock tool is running, leaving it to it",
                      m.nome, hz / 1000000u);
          m.posto_hz.store(0, std::memory_order_relaxed);
          continue;
        }
      }
      Por(m, posto);
      if (avisos++ < 8) {
        REXLOG_INFO("[relogios] {} was reset to {} MHz, back to {} MHz", m.nome, hz / 1000000u, posto / 1000000u);
      }
    }
  }
}

uint32_t Pedido(int mhz, bool boost, const Modulo& m) {
  uint64_t hz = mhz > 0 ? uint64_t(mhz) * 1000000u : (boost ? m.boost_hz : 0);
  return uint32_t(hz > m.teto_hz ? m.teto_hz : hz);
}

}  // namespace

void Iniciar() {
  const bool boost = REXCVAR_GET(fh2_switch_boost);
  g_mod[0].quer_hz = Pedido(REXCVAR_GET(fh2_switch_cpu_mhz), boost, g_mod[0]);
  g_mod[1].quer_hz = Pedido(REXCVAR_GET(fh2_switch_gpu_mhz), boost, g_mod[1]);
  g_mod[2].quer_hz = Pedido(REXCVAR_GET(fh2_switch_mem_mhz), boost, g_mod[2]);
  if (!g_mod[0].quer_hz && !g_mod[1].quer_hz && !g_mod[2].quer_hz) {
    REXLOG_INFO("[relogios] system clocks (fh2_switch_boost off)");
    return;
  }
  g_so_docked = REXCVAR_GET(fh2_switch_boost_docked_only);

  const char* gerente = ServicoRodando("hoc:clk") ? "Horizon OC" : ServicoRodando("sys:clk") ? "sys-clk" : nullptr;
  if (gerente && !REXCVAR_GET(fh2_switch_clock_force)) {
    REXLOG_INFO("[relogios] {} is running: clocks left to its profile (fh2_switch_clock_force overrides)", gerente);
    return;
  }
  if (hosversionAtLeast(8, 0, 0) && R_SUCCEEDED(clkrstInitialize())) {
    g_clkrst = 1;
  } else if (R_SUCCEEDED(pcvInitialize())) {
    g_clkrst = 0;
  } else {
    REXLOG_WARN("[relogios] neither clkrst nor pcv is available: clocks unchanged");
    return;
  }

  const bool docked = Docked();
  for (Modulo& m : g_mod) {
    if (!m.quer_hz) {
      continue;
    }
    if (g_clkrst) {
      const Result rc = clkrstOpenSession(&m.sessao, m.id, 3);
      if (R_FAILED(rc)) {
        REXLOG_WARN("[relogios] {}: no clkrst session (0x{:X})", m.nome, rc);
        continue;
      }
      m.aberto = true;
    }
    Ler(m, &m.antigo_hz);
    const uint32_t hz = Escolher(m, m.quer_hz);
    if (g_so_docked && !docked) {
      m.posto_hz.store(hz, std::memory_order_relaxed);  // the keeper sets it once docked
      REXLOG_INFO("[relogios] {} {} MHz now, {} MHz when docked", m.nome, m.antigo_hz / 1000000u, hz / 1000000u);
      continue;
    }
    if (m.antigo_hz >= hz) {
      REXLOG_INFO("[relogios] {} already at {} MHz (asked {}): left alone", m.nome, m.antigo_hz / 1000000u,
                  m.quer_hz / 1000000u);
      continue;
    }
    const Result rc = Por(m, hz);
    uint32_t agora = 0;
    Ler(m, &agora);
    if (R_SUCCEEDED(rc)) {
      m.posto_hz.store(agora ? agora : hz, std::memory_order_relaxed);
    }
    REXLOG_INFO("[relogios] {} {} -> {} MHz (asked {}){}", m.nome, m.antigo_hz / 1000000u, agora / 1000000u,
                m.quer_hz / 1000000u, R_FAILED(rc) ? " -- refused" : "");
  }

  ueventCreate(&g_acordar, true);
  appletHook(&g_hook, Gancho, nullptr);
  if (R_SUCCEEDED(threadCreate(&g_thread, Guardiao, nullptr, nullptr, 0x4000, 0x2C, -2)) &&
      R_SUCCEEDED(threadStart(&g_thread))) {
    g_thread_up = true;
  }
}

void Parar() {
  if (g_clkrst < 0) {
    return;
  }
  g_parar.store(true, std::memory_order_release);
  appletUnhook(&g_hook);
  ueventSignal(&g_acordar);
  if (g_thread_up) {
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_thread_up = false;
  }
  for (Modulo& m : g_mod) {
    if (m.posto_hz.load(std::memory_order_relaxed) && m.antigo_hz) {
      Por(m, m.antigo_hz);
    }
  }
  for (Modulo& m : g_mod) {
    if (m.aberto) {
      clkrstCloseSession(&m.sessao);
    }
  }
  if (g_clkrst) {
    clkrstExit();
  } else {
    pcvExit();
  }
  g_clkrst = -1;
}

}  // namespace fh2::relojes
