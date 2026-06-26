#pragma once

#include <filesystem>
#include <windows.h>
#include <shellapi.h>

namespace PkgInstaller {

void RunInstallDialog(HWND parent);
void HandleDroppedFiles(HWND parent, HDROP drop);
bool InstallPkgFile(HWND parent, const std::filesystem::path& pkg_file);

} // namespace PkgInstaller
