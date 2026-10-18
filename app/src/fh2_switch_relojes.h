// fh2 - CPU, GPU and memory clocks on the Switch (fh2_switch_relojes.cpp).
#pragma once

namespace fh2::relojes {

// Applies fh2_switch_boost / fh2_switch_*_mhz and starts the thread that keeps them through dock, focus and
// sleep changes. Once, after setup.
void Iniciar();
// Stops that thread and puts the old clocks back.
void Parar();

}  // namespace fh2::relojes
