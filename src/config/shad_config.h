#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ShadConfig {

struct InstallPaths {
    std::filesystem::path games_dir;
    std::filesystem::path addons_dir;
    std::filesystem::path update_patches_dir;
};

// Load game/addon install directories from shadPS4 config (config.json or legacy config.toml).
std::optional<InstallPaths> LoadInstallPaths();

std::filesystem::path GetUserConfigDir();
std::filesystem::path DefaultUpdatePatchesDir();
std::filesystem::path GetUpdatePatchesDir();
bool SaveUpdatePatchesDir(const std::filesystem::path& dir);

} // namespace ShadConfig
