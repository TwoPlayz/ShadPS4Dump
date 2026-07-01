#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <windows.h>
#include <shellapi.h>

namespace PkgInstaller {

inline constexpr const char* kInstallAbortedError = "Installation aborted.";

struct InstallProgress {
    int pkg_index = 0;
    int pkg_count = 0;
    int file_index = 0;
    int file_count = 0;
    int file_percent = 0;
    std::string stage;
    std::string file_name;
    std::filesystem::path pkg_path;
    std::filesystem::path extract_path;
};

using ProgressCallback = std::function<void(const InstallProgress&)>;
using CancelCallback = std::function<bool()>;

void RunInstallDialog(HWND parent);
void RunInstallGameDialog(HWND parent);
void HandleDroppedFiles(HWND parent, HDROP drop);
bool InstallPkgFile(HWND parent, const std::filesystem::path& pkg_file,
                    ProgressCallback progress = {}, CancelCallback should_cancel = {});
bool InstallPkgFiles(HWND parent, const std::vector<std::filesystem::path>& pkg_files,
                     ProgressCallback progress, std::string* last_error = nullptr,
                     CancelCallback should_cancel = {});

} // namespace PkgInstaller
