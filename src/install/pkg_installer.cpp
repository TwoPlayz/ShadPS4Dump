#include "install/pkg_installer.h"

#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "config/shad_config.h"
#include "core/file_format/pkg.h"
#include "hook/menu_hook.h"
#include "install/pkg_router.h"

namespace PkgInstaller {

namespace {

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::vector<std::filesystem::path> PickPkgFiles(HWND parent) {
    std::vector<std::filesystem::path> files;
    wchar_t buffer[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"PKG Files\0*.pkg\0All Files\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = L"Select PKG file(s) to install";

    if (!GetOpenFileNameW(&ofn)) {
        return files;
    }

    const wchar_t* ptr = buffer;
    const std::wstring directory = ptr;
    ptr += directory.size() + 1;
    if (*ptr == L'\0') {
        files.emplace_back(directory);
        return files;
    }

    while (*ptr) {
        files.emplace_back(std::filesystem::path(directory) / ptr);
        ptr += wcslen(ptr) + 1;
    }
    return files;
}

bool ExtractPkg(HWND parent, const std::filesystem::path& pkg_file,
                const std::filesystem::path& extract_path) {
    PKG pkg;
    std::string failreason;
    if (!pkg.Open(pkg_file, failreason)) {
        MessageBoxW(parent, ToWide(failreason).c_str(), L"PKG Error", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!pkg.Extract(pkg_file, extract_path, failreason)) {
        MessageBoxW(parent, ToWide(failreason).c_str(), L"PKG Error", MB_OK | MB_ICONERROR);
        return false;
    }

    const int file_count = pkg.GetNumberOfFiles();
    for (int i = 0; i < file_count; ++i) {
        pkg.ExtractFiles(i);
    }
    return true;
}

} // namespace

bool InstallSinglePkg(HWND parent, const std::filesystem::path& pkg_file) {
    const auto paths = ShadConfig::LoadInstallPaths();
    if (!paths) {
        MessageBoxW(parent,
                    L"Could not read game install directories from shadPS4 config.\n"
                    L"Configure them via Game -> Game Install Directory first.",
                    L"PKG Install", MB_OK | MB_ICONWARNING);
        return false;
    }

    PkgRouter::RouteResult route;
    if (PkgRouter::ResolveInstallPath(pkg_file, paths->games_dir, paths->addons_dir, route,
                                      parent) != PkgRouter::OverwriteDecision::Proceed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(route.extract_path, ec);

    if (!ExtractPkg(parent, pkg_file, route.extract_path)) {
        return false;
    }

    std::wstringstream ss;
    ss << L"PKG installed successfully to:\n" << route.extract_path.wstring();
    MessageBoxW(parent, ss.str().c_str(), L"Extraction Finished", MB_OK | MB_ICONINFORMATION);
    MenuHook::TriggerRefreshGameList(parent);
    return true;
}

void InstallMany(HWND parent, const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }

    for (const auto& file : files) {
        InstallSinglePkg(parent, file);
    }
}

bool InstallPkgFile(HWND parent, const std::filesystem::path& pkg_file) {
    return InstallSinglePkg(parent, pkg_file);
}

void RunInstallDialog(HWND parent) {
    auto files = PickPkgFiles(parent);
    if (files.empty()) {
        return;
    }

    std::thread([parent, files = std::move(files)]() { InstallMany(parent, files); }).detach();
}

void HandleDroppedFiles(HWND parent, HDROP drop) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::filesystem::path> files;
    files.reserve(count);

    for (UINT i = 0; i < count; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len, L'\0');
        DragQueryFileW(drop, i, path.data(), len + 1);
        if (path.size() >= 4) {
            const auto ext = std::filesystem::path(path).extension().wstring();
            if (_wcsicmp(ext.c_str(), L".pkg") == 0) {
                files.emplace_back(path);
            }
        }
    }

    if (!files.empty()) {
        std::thread([parent, files = std::move(files)]() { InstallMany(parent, files); }).detach();
    }
}

} // namespace PkgInstaller
