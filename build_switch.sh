#!/usr/bin/env bash
# Builds the Nintendo Switch homebrew (app/out/switch/fh2.nro) with devkitPro's devkitA64 and libnx.
#
#   FH2_SWITCH_NVK_SDK=<mesa horizon sdk> ./build_switch.sh
#
# Needs:
#   - the generated game code in app/generated (./build_pc.sh, or tools/codegen.sh with a host rexglue): a cross
#     build cannot run the code generator;
#   - devkitPro (DEVKITPRO, default /opt/devkitpro) with devkitA64, libnx, switch-zlib, switch-libzstd and
#     switch-libexpat (dkp-pacman -S switch-dev switch-zlib switch-libzstd switch-libexpat);
#   - the Mesa Horizon SDK with the static NVK driver: the folder holding lib/libvulkan.a, built from
#     https://github.com/danfromtico/mesa-switch (nfsmw-nx's docs/mesa.md explains how, and its
#     mesa/mesa-switch-nfsmw.patch). Given with FH2_SWITCH_NVK_SDK.
# JOBS limits the parallel compiles (default 4): GCC needs several GB per file of translated code.
set -euo pipefail

RAIZ=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITPRO
NVK_SDK=${FH2_SWITCH_NVK_SDK:-}
JOBS=${JOBS:-4}
BUILD_TYPE=${BUILD_TYPE:-Release}
SAIDA=${SAIDA:-$RAIZ/app/out/switch}

[ -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" ] || {
  echo "ERROR: devkitA64 not found in $DEVKITPRO (set DEVKITPRO)." >&2; exit 1; }
[ -n "$NVK_SDK" ] && [ -f "$NVK_SDK/lib/libvulkan.a" ] || {
  echo "ERROR: set FH2_SWITCH_NVK_SDK to the Mesa Horizon SDK folder that holds lib/libvulkan.a." >&2; exit 1; }
[ -f "$RAIZ/app/generated/default/sources.cmake" ] || {
  echo "ERROR: no generated code in app/generated; run ./build_pc.sh (or tools/codegen.sh app) first." >&2; exit 1; }
if [ ! -f "$RAIZ/sdk/thirdparty/CMakeLists.txt" ] || [ ! -d "$RAIZ/sdk/thirdparty/fmt" ]; then
  echo "[info] fetching the SDK's third-party sources..."
  python3 "$RAIZ/tools/fetch_thirdparty.py"
fi

cmake -S "$RAIZ/app" -B "$SAIDA" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="$RAIZ/tools/switch/switch-devkitA64.cmake" \
  -DDEVKITPRO="$DEVKITPRO" -DREXSDK_DIR="$RAIZ/sdk" -DREXGLUE_SWITCH_NVK_SDK="$NVK_SDK" "$@"
cmake --build "$SAIDA" -j "$JOBS"

echo "[ok] $SAIDA/fh2.nro"
echo "     Copy it to sdmc:/switch/fh2/ with the extracted game in sdmc:/switch/fh2/game_root/, and start it"
echo "     with title takeover (hold R while opening a game)."
