#pragma once

#include <filesystem>
#include <vector>
#include <windows.h>

namespace PkgInstallProgressWin32 {

void RunInstall(HWND owner, const std::vector<std::filesystem::path>& pkg_files);

} // namespace PkgInstallProgressWin32
