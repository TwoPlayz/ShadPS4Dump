#include "plugin_module.h"

namespace PluginModule {

namespace {

HMODULE g_module = nullptr;

} // namespace

void Set(HMODULE module) {
    g_module = module;
}

HMODULE Handle() {
    return g_module ? g_module : GetModuleHandleW(nullptr);
}

} // namespace PluginModule
