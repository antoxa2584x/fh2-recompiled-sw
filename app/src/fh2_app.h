// fh2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>

#include <rex/ui/presenter.h>

#include "fh2_captura.h"
#include "fh2_teste_entrada.h"
#include "nfsmw_nativo_sistema.h"

#if REX_PLATFORM_SWITCH
#include "fh2_switch_relojes.h"
#endif

#if REX_PLATFORM_ANDROID
// android/app/src/main/cpp/android_rendimiento.cpp: keeps the game threads off the slow cores.
extern "C" void fh2_android_nucleos_grandes_aplicar();
extern "C" void fh2_android_afinidade_iniciar();
#endif

void Fh2PerfilStart();  // fh2_perfil.cpp

class Fh2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fh2App>(new Fh2App(ctx, "fh2",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  void OnPreSetup(rex::RuntimeConfig& config) override {
    // The game needs the console's cache partition: after the first race it creates
    // cache:\replay_stream (rewind / replays), and without the partition it hangs on the loading
    // screen reading a null pointer. Mounted on user/cache_partition.
    rex::cvar::SetFlagByName("mount_cache", "true");
#if REX_PLATFORM_ANDROID
    fh2_android_nucleos_grandes_aplicar();
#endif
    // Native renderer (src/nativo, docs/renderizador_nativo.md): with nfsmw_renderizador = nativo the graphics
    // system is its own and the GPU emulation plugin is not loaded (ReXApp only loads it when config.graphics is
    // empty). The default is still the emulation.
    if (nfsmw::nativo::Activo()) {
      config.graphics = nfsmw::nativo::CrearSistemaGrafico();
    }
#if REX_PLATFORM_SWITCH
    // The Android launcher and the PC scripts pass --gpu_plugin=xenos; the homebrew menu passes nothing, and
    // without a plugin the game's GPU commands go nowhere and it waits forever at 0 fps. The emulation is the
    // default here too (fh2.toml can choose the native renderer, or another plugin).
    if (!config.graphics && config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
#endif
    fh2::teste::EnvolverEntrada(config);
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  void OnPostSetup() override {
#if REX_PLATFORM_SWITCH
    fh2::relojes::Iniciar();  // fh2_switch_relojes.cpp: the docked clocks in both modes unless fh2_switch_boost is off
#endif
    Fh2PerfilStart();  // fh2_perfil.cpp, off unless fh2_perfil_s is set
#if REX_PLATFORM_ANDROID
    fh2_android_afinidade_iniciar();  // android_rendimiento.cpp, off unless fh2_android_afinidade is set
#endif
    // PNG captures of the game image for unattended tests (fh2_captura_cada_s; off by default).
    fh2::captura::Arrancar([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphics = rt ? rt->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override {
    fh2::captura::Parar();
#if REX_PLATFORM_SWITCH
    fh2::relojes::Parar();
#endif
  }
#if REX_PLATFORM_SWITCH
  // The homebrew menu starts the NRO without arguments: the game is the game_root folder next to it
  // (sdmc:/switch/fh2/game_root), unless --game_data_root says otherwise. This runs before fh2.toml is read, so
  // the toml cannot set it.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    std::error_code ec;
    // Replacement game files (game_data_overrides): sdmc:/switch/fh2/overrides/<path in the game>, for example
    // media/tracks/Anthem/TrackSettings.xml with lighter detail settings; the game folder stays as extracted.
    const auto substituicoes = rex::filesystem::GetExecutableFolder() / "overrides";
    if (rex::cvar::GetFlagByName("game_data_overrides").empty() && std::filesystem::is_directory(substituicoes, ec)) {
      rex::cvar::SetFlagByName("game_data_overrides", substituicoes.string());
    }
    if (!paths.game_data_root.empty()) {
      return;
    }
    const auto game_root = rex::filesystem::GetExecutableFolder() / "game_root";
    if (std::filesystem::is_directory(game_root, ec)) {
      paths.game_data_root = game_root;
    }
  }
#endif
};
