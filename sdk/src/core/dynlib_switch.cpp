/**
 * @file        rex/core/dynlib_switch.cpp
 * @brief       DynamicLibrary backend for Horizon: there is no dynamic loading
 *
 * An NRO cannot load libraries at run time: dlopen does not exist and everything is
 * linked statically. The one library this backend can "load" is one linked into the
 * executable that registered itself with RegisterStaticLibrary: the recompiled XEX
 * modules (kernel_state.cpp loads them by file name and looks up ReXModule_Register).
 * For any other path it returns "not loaded", because the other callers already handle
 * that case:
 *
 *   - gpu_plugin_loader.cpp, which on Switch must be resolved through static
 *     registration (see rex::system::LoadGpuPlugin)
 *   - the optional RenderDoc and SPIRV-Tools loaders, which make no sense on the
 *     console
 *
 * If a third caller that depends on dlopen is ever added, it will show up here as a
 * Load() that returns false, not as a link error.
 */

#include <rex/platform.h>
#include <rex/platform/dynlib.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static_assert(REX_PLATFORM_SWITCH, "This file is Nintendo Switch-only");

namespace rex::platform {

namespace {

struct StaticLibrary {
  std::string file_name;
  const StaticLibrarySymbol* symbols;
  size_t count;
};

// Filled from static constructors, so built on first use rather than at an unknown point of static initialization.
// Entries are never removed: a handle is a pointer to one.
std::mutex& StaticLibrariesMutex() {
  static std::mutex mutex;
  return mutex;
}

std::vector<StaticLibrary*>& StaticLibraries() {
  static std::vector<StaticLibrary*> libraries;
  return libraries;
}

}  // namespace

void RegisterStaticLibrary(const char* file_name, const StaticLibrarySymbol* symbols, size_t count) {
  std::lock_guard<std::mutex> lock(StaticLibrariesMutex());
  StaticLibraries().push_back(new StaticLibrary{file_name, symbols, count});
}

DynamicLibrary::~DynamicLibrary() {
  Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : handle_(other.handle_) {
  other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

bool DynamicLibrary::Load(const std::filesystem::path& path, SymbolResolution mode) {
  (void)mode;
  Close();
  const std::string file_name = path.filename().string();
  std::lock_guard<std::mutex> lock(StaticLibrariesMutex());
  for (StaticLibrary* library : StaticLibraries()) {
    if (library->file_name == file_name) {
      handle_ = library;
      return true;
    }
  }
  return false;
}

void DynamicLibrary::Close() {
  handle_ = nullptr;
}

void* DynamicLibrary::GetRawSymbol(const char* name) const {
  const auto* library = static_cast<const StaticLibrary*>(handle_);
  if (!library || !name) {
    return nullptr;
  }
  for (size_t i = 0; i < library->count; ++i) {
    if (std::strcmp(library->symbols[i].name, name) == 0) {
      return library->symbols[i].address;
    }
  }
  return nullptr;
}

}  // namespace rex::platform
