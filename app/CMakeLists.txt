# fh2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fh2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FH2_SOURCES
    src/main.cpp
    # Tests on the PC: virtual pad driven by a button script (cvar fh2_teste_botoes).
    src/fh2_teste_entrada.cpp
    # PNG captures of the game image every N seconds (cvar fh2_captura_cada_s).
    src/fh2_captura.cpp
    # Guards on game functions (null checks the console never needed).
    src/fh2_correcoes.cpp
    src/fh2_desempenho.cpp
    src/fh2_perfil.cpp
    src/fh2_censo.cpp
    # Native renderer (cvar fh2_renderizador = nativo): NFSMW Android Evolved's, built for FH2 with FH2_RECOMP
    # (src/nativo/README.md, docs/renderizador_nativo.md), FH2's hooks and the answers it asks of NFSMW's modules.
    src/nativo/nfsmw_nativo_sistema.cpp
    src/nativo/nfsmw_nativo_destinos.cpp
    src/nativo/nfsmw_nativo_dibujos.cpp
    src/nativo/nfsmw_nativo_shaders.cpp
    src/nativo/nfsmw_nativo_ganchos.cpp
    src/nativo/nfsmw_nativo_texturas_pool.cpp
    src/nativo/nfsmw_nativo_captura.cpp
    src/nativo/nfsmw_shader_library.cpp
    src/nativo/nfsmw_espera_anillo.cpp
    src/nativo/nfsmw_ajustes_graficos.cpp
    src/fh2_nativo_ganchos.cpp
    src/fh2_nativo_enlaces.cpp
)

if(ANDROID)
    # SDLActivity loads SDL_main from libmain.so (android/app/src/main/cpp/CMakeLists.txt builds this).
    add_library(fh2 SHARED ${FH2_SOURCES})
    set_target_properties(fh2 PROPERTIES OUTPUT_NAME main)
elseif(WIN32)
    add_executable(fh2 WIN32 ${FH2_SOURCES})
else()
    add_executable(fh2 ${FH2_SOURCES})
endif()

rexglue_setup_target(fh2 GPU_PLUGINS xenos)

# PC build with the SDK as a subdirectory: rex_app.cpp includes imgui.h and rexruntime links imgui
# privately, so its include path does not reach the executable (same fix as nfsmw-android). The same
# goes for renderdoc_app.h, which rex/ui/vulkan/instance.h includes outside the Switch. On Switch
# rexglue_switch.cmake already adds imgui's path.
if(TARGET imgui AND NOT REXGLUE_PLATFORM_SWITCH)
    target_include_directories(fh2 PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
if(TARGET renderdoc AND NOT REXGLUE_PLATFORM_SWITCH)
    target_include_directories(fh2 PRIVATE $<TARGET_PROPERTY:renderdoc,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

# Native renderer: FH2's branches, its folder, the SDK's xxHash (shader library fingerprints) and the SDK
# presenter's precompiled SPIR-V (it draws the game image into the presenter output).
target_compile_definitions(fh2 PRIVATE FH2_RECOMP=1)
target_include_directories(fh2 PRIVATE src/nativo)
if(REXSDK_DIR)
    target_include_directories(fh2 PRIVATE "${REXSDK_DIR}/thirdparty/xxHash" "${REXSDK_DIR}/src/ui/shaders")
endif()

# Nintendo Switch (./build_switch.sh): fh2.nro next to the ELF. On the SD card it goes in sdmc:/switch/fh2/, with
# the extracted game_root/ folder (and fh2_shaders.nfsp for the native renderer) beside it. Launched with title
# takeover: in applet mode the process gets about 400 MB (sdk/src/ui/runtime_switch.cpp).
if(REXGLUE_PLATFORM_SWITCH)
    # CPU, GPU and memory clocks (fh2_switch_boost, on by default: the docked rates in both modes).
    target_sources(fh2 PRIVATE src/fh2_switch_relojes.cpp)
    # -O3 bloats the translated code, and the Cortex-A57 has 48 KB of instruction cache (nfsmw-nx measured smaller,
    # faster code with -O2). It comes after CMAKE_CXX_FLAGS_RELEASE's -O3, so it wins.
    option(FH2_SWITCH_RECOMP_O2 "Compile the translated game code with -O2 instead of -O3 (Switch)" ON)
    if(FH2_SWITCH_RECOMP_O2)
        foreach(alvo fh2_recomp ${REXGLUE_DLL_MODULE_TARGETS})
            if(TARGET ${alvo})
                target_compile_options(${alvo} PRIVATE -O2)
            endif()
        endforeach()
    endif()
    # LTO across the game code and ours (with tools/llamadas_directas.py the compiler can inline small game
    # functions across files). Off by default: the link needs a lot of RAM (16 GB with -flto=2 for NFSMW).
    option(FH2_SWITCH_LTO "LTO on the game code and the app (Switch)" OFF)
    if(FH2_SWITCH_LTO)
        foreach(alvo fh2_recomp fh2)
            target_compile_options(${alvo} PRIVATE -flto -fno-fat-lto-objects)
        endforeach()
        target_link_options(fh2 PRIVATE -flto=2 -flto-partition=balanced)
    endif()
    # Profile-guided optimization (PGO) in two steps, as nfsmw-nx does it (docs/switch.md):
    #   generar: instrumented build; the game dumps the profile to sdmc:/switch/fh2/pgo every 2 minutes
    #            (src/fh2_switch_pgo.cpp);
    #   usar:    rebuilds with those .gcda files copied to FH2_SWITCH_PGO_DIR.
    # Both in the same build folder: each .gcda is named after its object's path relative to it
    # (-fprofile-prefix-path). -fno-profile-values: libgcov's value profiling reads the TLS through tpidr_el0, which is 0
    # on Horizon (libnx uses -mtp=soft), and the instrumented build crashed at startup in nfsmw-nx.
    set(FH2_SWITCH_PGO "" CACHE STRING "Profile-guided optimization on Switch: empty, generar or usar")
    get_filename_component(FH2_SWITCH_PGO_DIR_PADRAO "${CMAKE_CURRENT_SOURCE_DIR}/../pgo/switch" ABSOLUTE)
    set(FH2_SWITCH_PGO_DIR "${FH2_SWITCH_PGO_DIR_PADRAO}" CACHE PATH "Folder of the Switch PGO profile (.gcda)")
    target_sources(fh2 PRIVATE src/fh2_switch_pgo.cpp)
    if(FH2_SWITCH_PGO STREQUAL "generar")
        set(FH2_PGO_OPCOES -fprofile-generate=${FH2_SWITCH_PGO_DIR} "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                           -fprofile-update=single -fno-profile-values)
        target_link_options(fh2 PRIVATE -fprofile-generate)
        target_compile_definitions(fh2 PRIVATE FH2_PGO_GERAR=1 "FH2_PGO_DIR=\"${FH2_SWITCH_PGO_DIR}\"")
    elseif(FH2_SWITCH_PGO STREQUAL "usar")
        # -fprofile-correction: the counters are not atomic (-fprofile-update=single), and functions run by several
        # threads at once end up with inconsistent counts that GCC would otherwise reject as a corrupt profile.
        set(FH2_PGO_OPCOES -fprofile-use=${FH2_SWITCH_PGO_DIR} "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                           -fprofile-partial-training -fno-profile-values -fprofile-correction -Wno-missing-profile
                           -Wno-error=coverage-mismatch)
        target_link_options(fh2 PRIVATE -fprofile-use -fprofile-partial-training -fprofile-correction)
    elseif(FH2_SWITCH_PGO)
        message(FATAL_ERROR "FH2_SWITCH_PGO must be empty, generar or usar (it is '${FH2_SWITCH_PGO}')")
    endif()
    if(FH2_SWITCH_PGO)
        foreach(alvo fh2_recomp fh2 ${REXGLUE_DLL_MODULE_TARGETS})
            if(TARGET ${alvo})
                target_compile_options(${alvo} PRIVATE ${FH2_PGO_OPCOES})
            endif()
        endforeach()
    endif()
    # Homebrew menu icon: a 256x256 JPEG. Not distributed (it would be the game's art); without
    # tools/switch/icon.jpg the NRO gets libnx's default icon.
    set(FH2_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../tools/switch/icon.jpg")
    set(FH2_SWITCH_ICON_ARG)
    if(EXISTS "${FH2_SWITCH_ICON}")
        set(FH2_SWITCH_ICON_ARG ICON "${FH2_SWITCH_ICON}")
    endif()
    rexglue_switch_add_nro(fh2
        NAME "FH2 Recompiled"
        AUTHOR "FH2 Recompiled contributors"
        VERSION "0.1.0"
        ${FH2_SWITCH_ICON_ARG})
endif()
