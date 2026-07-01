#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ShadConfig {

struct InstallPaths {
    std::filesystem::path games_dir;
    std::filesystem::path addons_dir;
};

// Load game/addon install directories from shadPS4 config (config.json or legacy config.toml).
std::optional<InstallPaths> LoadInstallPaths();

std::filesystem::path GetUserConfigDir();

} // namespace ShadConfig
