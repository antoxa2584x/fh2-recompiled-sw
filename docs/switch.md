# Nintendo Switch (homebrew NRO)

Experimental. The Switch build is a single static NRO made with devkitPro's devkitA64 (GCC) and libnx, with
Mesa's NVK as the Vulkan driver, linked in statically. The SDK's Switch backend (memory, threads, audio, input,
window, NRO packaging) comes from [nfsmw-nx](https://github.com/StevensND/nfsmw-nx), which this project's SDK was
imported from.

## What you need

- Linux (or WSL2) with everything [Building from source](../README.md#building-from-source) needs: the code
  generator runs on the PC, never in the cross build.
- devkitPro with devkitA64 and libnx, and the portlibs NVK links against:

  ```sh
  sudo dkp-pacman -S switch-dev switch-zlib switch-libzstd switch-libexpat
  ```

- The Mesa Horizon SDK: the static NVK driver for the Switch's GPU. There is no prebuilt release. Build it from
  [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch) (tested at commit `1a8c1a66d6f`) with
  nfsmw-nx's `mesa/mesa-switch-nfsmw.patch` applied; nfsmw-nx's `docs/mesa.md` explains the build. What the build
  below needs is the folder that holds `lib/libvulkan.a`.
- About 20 GB of RAM for a parallel build: GCC needs a few GB per file of translated code (`JOBS` limits it).

## Building

1. Extract your disc and run the code generator (steps 1 to 3 of
   [Building from source](../README.md#building-from-source); `./build_pc.sh` does all of it). `tools/codegen.sh`
   also runs the two passes the Switch build depends on:
   - `tools/llamadas_directas.py`: calls to game functions without a hook go straight to `__imp__sub_X`, which GCC
     can inline (with GCC every `sub_X` is a weak alias);
   - `tools/nomes_modulos.py`: the DLL modules (XMediaFacade, SpeechFacade) get their own names for the functions
     every module defines (`xstart`, `__savegprlr_N`, ...), since the NRO links every module into one executable.
2. Build the NRO:

   ```sh
   FH2_SWITCH_NVK_SDK=/path/to/mesa-horizon-sdk JOBS=8 ./build_switch.sh
   ```

   It writes `app/out/switch/fh2.nro`. Options (passed to `./build_switch.sh` as `-D...`):
   - `FH2_SWITCH_RECOMP_O2` (on): the translated code with `-O2`, smaller than `-O3` for the A57's instruction
     cache;
   - `FH2_SWITCH_LTO` (off): LTO over the game code and the app; the link needs a lot of RAM.

   An icon for the homebrew menu can be put in `tools/switch/icon.jpg` (256x256 JPEG); it is not distributed.

### Profile-guided optimization (PGO)

Optional, as nfsmw-nx does it: GCC reorders and inlines the translated game code by how the game really runs.

1. Instrumented build, in a folder of its own (the profile is matched by object path, so both builds must use it):

   ```sh
   SAIDA=$PWD/app/out/switch-pgo FH2_SWITCH_NVK_SDK=... ./build_switch.sh -DFH2_SWITCH_PGO=generar
   ```

2. Play it (it is slower: every branch is counted). Every 2 minutes it writes its counters to
   `sdmc:/switch/fh2/pgo/` and starts over; `despejos.txt` there lists each dump and how long it took. A dump takes
   over a minute on the SD card (about 340 files), so play at least 8-10 minutes to get several: what is played
   after the last dump is lost.
3. Copy `sdmc:/switch/fh2/pgo/*.gcda` to `pgo/switch` (ignored by Git) and build with the profile:

   ```sh
   SAIDA=$PWD/app/out/switch-pgo FH2_SWITCH_NVK_SDK=... ./build_switch.sh -DFH2_SWITCH_PGO=usar
   ```

## Running

```
sdmc:/switch/fh2/fh2.nro
sdmc:/switch/fh2/game_root/          the extracted game (default.xex, media/, ...)
sdmc:/switch/fh2/fh2_shaders.nfsp    optional: the native renderer's shader library
sdmc:/switch/fh2/fh2.toml            optional: settings (cvars)
```

- Start it with **title takeover** (hold R while opening a game, then pick it in the homebrew menu). In applet
  mode the process gets about 400 MB and the game cannot start.
- The game must be extracted: a disc image (.iso) is refused on Switch (the SDK would read the whole file into
  RAM), and an 8 GB file does not fit on a FAT32 card anyway.
- Logs are written to `sdmc:/switch/fh2/logs`; a crash leaves `sdmc:/switch/rex_crash.log`.

## Clocks

By default the NRO runs the CPU, GPU and memory at the docked rates (1785 / 768 / 1600 MHz) in **both** modes
(`app/src/fh2_switch_relojes.cpp`, ported from the FlatOut 2 port's `FO2_DOCK_BOOST`): more heat and battery in
handheld. A thread puts them back after the system resets them (dock/undock, HOME, sleep), and the old clocks
come back on exit. In `fh2.toml`:

```toml
fh2_switch_boost = false               # the system's clocks
fh2_switch_boost_docked_only = true    # boost while docked only
fh2_switch_cpu_mhz = 1785              # other rates (also _gpu_mhz, _mem_mhz; 0 = boost or system)
fh2_switch_clock_force = true          # even when sys-clk / Horizon OC is running
```

With sys-clk or Horizon OC running nothing is touched unless forced: their profile decides.

## Status

- Builds: `fh2.nro` is about 110 MB (107 MB of code with `-O2`), with the game, both DLL modules and the native
  renderer linked in.
- Eden (emulator): the NRO starts, maps the guest memory, mounts `game_root` and the cache partition, then stalls:
  the game's writes into the guest memory window land on pages Eden does not consider mapped (the window is made
  of `svcMapProcessCodeMemory` / `svcMapProcessMemory` mirrors, which Eden does not emulate faithfully), and with
  `cpuopt_ignore_memory_aborts` on they are silently dropped. Not a useful test bed for this port.
- Console: not tested yet.

## Differences from the other platforms

- The DLL modules are linked into the executable and found by name (`RegisterStaticLibrary`,
  `sdk/src/core/dynlib_switch.cpp`) instead of being loaded as shared libraries.
- Waits that use a futex on Linux use the kernel's address arbiter (`rex/thread/futex.h`).
- Debug tools that need Linux are off: the sampling profiler (`fh2_perfil_s`) and the census' argument reads
  (`fh2_censo`).
- The native renderer has none of nfsmw-nx's Switch tuning yet (thread placement, texture budget, Mesa cache).
