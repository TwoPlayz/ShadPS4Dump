#include "config/shad_config.h"

#include <fstream>
#include <Shlobj.h>
#include <toml.hpp>
#include <windows.h>
#include <nlohmann/json.hpp>

namespace ShadConfig {

namespace fs = std::filesystem;

fs::path GetUserConfigDir() {
    const fs::path portable = fs::current_path() / "user";
    if (fs::exists(portable)) {
        return portable;
    }
    TCHAR appdata[MAX_PATH] = {0};
    SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    return fs::path(appdata) / "shadPS4";
}

static std::optional<InstallPaths> FromJson(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return std::nullopt;
    }

    if (!root.contains("General")) {
        return std::nullopt;
    }

    const auto& general = root["General"];
    InstallPaths result;

    if (general.contains("install_dirs") && general["install_dirs"].is_array()) {
        for (const auto& entry : general["install_dirs"]) {
            if (!entry.contains("path") || !entry.contains("enabled")) {
                continue;
            }
            if (!entry["enabled"].get<bool>()) {
                continue;
            }
            result.games_dir = fs::path(entry["path"].get<std::string>());
            break;
        }
    }

    if (general.contains("addon_install_dir") && general["addon_install_dir"].is_string()) {
        result.addons_dir = fs::path(general["addon_install_dir"].get<std::string>());
    }

    if (general.contains("update_patches_install_dir") &&
        general["update_patches_install_dir"].is_string()) {
        result.update_patches_dir =
            fs::path(general["update_patches_install_dir"].get<std::string>());
    }

    if (result.games_dir.empty()) {
        return std::nullopt;
    }
    if (result.addons_dir.empty()) {
        result.addons_dir = result.games_dir / "DLC";
    }
    return result;
}

static std::optional<InstallPaths> FromToml(const fs::path& path) {
    try {
        std::ifstream ifs(path);
        const auto root = toml::parse(ifs, path.string());

        InstallPaths result;

        if (root.contains("General")) {
            const auto& general = root.at("General");
            if (general.contains("install_dirs") && general.at("install_dirs").is_array()) {
                for (const auto& entry : general.at("install_dirs").as_array()) {
                    if (!entry.is_table()) {
                        continue;
                    }
                    const auto& table = entry.as_table();
                    auto path_it = table.find("path");
                    auto enabled_it = table.find("enabled");
                    if (path_it == table.end()) {
                        continue;
                    }
                    bool enabled = true;
                    if (enabled_it != table.end() && enabled_it->second.is_boolean()) {
                        enabled = enabled_it->second.as_boolean();
                    }
                    if (!enabled) {
                        continue;
                    }
                    result.games_dir = fs::path(toml::get<std::string>(path_it->second));
                    break;
                }
            }
            if (general.contains("addon_install_dir")) {
                result.addons_dir =
                    fs::path(toml::get<std::string>(general.at("addon_install_dir")));
            }
            if (general.contains("update_patches_install_dir")) {
                result.update_patches_dir =
                    fs::path(toml::get<std::string>(general.at("update_patches_install_dir")));
            }
        }

        if (result.games_dir.empty() && root.contains("GUI")) {
            const auto& gui = root.at("GUI");
            const auto install_dirs = toml::find_or<std::vector<std::string>>(gui, "installDirs", {});
            if (!install_dirs.empty()) {
                result.games_dir = fs::path(install_dirs.front());
            }
            if (gui.contains("addonInstallDir")) {
                result.addons_dir = fs::path(toml::get<std::string>(gui.at("addonInstallDir")));
            }
            if (gui.contains("updatePatchesInstallDir")) {
                result.update_patches_dir =
                    fs::path(toml::get<std::string>(gui.at("updatePatchesInstallDir")));
            }
        }

        if (result.games_dir.empty()) {
            return std::nullopt;
        }
        if (result.addons_dir.empty()) {
            result.addons_dir = result.games_dir / "DLC";
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<InstallPaths> LoadInstallPaths() {
    const auto user_dir = GetUserConfigDir();
    const auto json_path = user_dir / "config.json";
    if (fs::exists(json_path)) {
        if (auto paths = FromJson(json_path)) {
            return paths;
        }
    }
    const auto toml_path = user_dir / "config.toml";
    if (fs::exists(toml_path)) {
        return FromToml(toml_path);
    }
    return std::nullopt;
}

fs::path DefaultUpdatePatchesDir() {
    return GetUserConfigDir() / "update_patches";
}

static std::optional<fs::path> ReadUpdatePatchesDirFromJson(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (...) {
        return std::nullopt;
    }

    if (!root.contains("General")) {
        return std::nullopt;
    }

    const auto& general = root["General"];
    if (!general.contains("update_patches_install_dir") ||
        !general["update_patches_install_dir"].is_string()) {
        return std::nullopt;
    }

    const auto value = general["update_patches_install_dir"].get<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return fs::path(value);
}

static std::optional<fs::path> ReadUpdatePatchesDirFromToml(const fs::path& path) {
    try {
        std::ifstream ifs(path);
        const auto root = toml::parse(ifs, path.string());

        if (root.contains("General")) {
            const auto& general = root.at("General");
            if (general.contains("update_patches_install_dir")) {
                const auto value =
                    toml::get<std::string>(general.at("update_patches_install_dir"));
                if (!value.empty()) {
                    return fs::path(value);
                }
            }
        }

        if (root.contains("GUI")) {
            const auto& gui = root.at("GUI");
            if (gui.contains("updatePatchesInstallDir")) {
                const auto value = toml::get<std::string>(gui.at("updatePatchesInstallDir"));
                if (!value.empty()) {
                    return fs::path(value);
                }
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

fs::path GetUpdatePatchesDir() {
    const auto user_dir = GetUserConfigDir();
    const auto json_path = user_dir / "config.json";
    if (fs::exists(json_path)) {
        if (auto path = ReadUpdatePatchesDirFromJson(json_path)) {
            return *path;
        }
    }

    const auto toml_path = user_dir / "config.toml";
    if (fs::exists(toml_path)) {
        if (auto path = ReadUpdatePatchesDirFromToml(toml_path)) {
            return *path;
        }
    }

    return DefaultUpdatePatchesDir();
}

bool SaveUpdatePatchesDir(const fs::path& dir) {
    if (dir.empty()) {
        return false;
    }

    const auto json_path = GetUserConfigDir() / "config.json";
    nlohmann::json root = nlohmann::json::object();

    if (fs::exists(json_path)) {
        std::ifstream in(json_path);
        if (in) {
            try {
                in >> root;
            } catch (...) {
                root = nlohmann::json::object();
            }
        }
    }

    if (!root.contains("General") || !root["General"].is_object()) {
        root["General"] = nlohmann::json::object();
    }

    root["General"]["update_patches_install_dir"] = dir.string();

    std::error_code ec;
    fs::create_directories(json_path.parent_path(), ec);

    std::ofstream out(json_path);
    if (!out) {
        return false;
    }

    out << root.dump(2);
    return static_cast<bool>(out);
}

} // namespace ShadConfig
