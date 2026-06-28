#pragma once

#include <filesystem>
#include <string>
#include <windows.h>

namespace PkgRouter {

struct RouteResult {
    std::filesystem::path extract_path;
    bool is_patch = false;
    bool is_dlc = false;
    std::string title_id;
    std::string entitlement_label;
};

enum class OverwriteDecision {
    Proceed,
    Cancel,
};

// Determine extraction destination and prompt user when installs already exist.
OverwriteDecision ResolveInstallPath(const std::filesystem::path& pkg_file,
                                     const std::filesystem::path& games_dir,
                                     const std::filesystem::path& addons_dir,
                                     RouteResult& out, HWND parent);

// Returns true when the base game for a title ID is installed (eboot.bin present).
bool IsBaseGameInstalled(const std::string& title_id);

void ShowDeltaPkgNotSupported(HWND parent);

} // namespace PkgRouter
