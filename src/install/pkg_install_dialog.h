#pragma once

#include <filesystem>
#include <vector>
#include <windows.h>

namespace PkgInstallDialog {

void Run(const std::vector<std::filesystem::path>& pkg_files, HWND parent_hwnd);

} // namespace PkgInstallDialog
