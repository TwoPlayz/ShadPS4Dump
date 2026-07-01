#include "install/pkg_installer.h"

#include <commdlg.h>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

#include "config/shad_config.h"
#include "common/path_util.h"
#include "core/file_format/pkg.h"
#include "hook/menu_hook.h"
#include "install/pkg_install_dialog.h"
#include "install/pkg_merge.h"
#include "install/pkg_passcode.h"
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

void ReportProgress(const ProgressCallback& progress, const InstallProgress& state) {
    if (progress) {
        progress(state);
    }
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

bool ExtractPkg(const std::filesystem::path& pkg_file, const std::filesystem::path& extract_path,
                std::string& failreason, const ProgressCallback& progress, int pkg_index,
                int pkg_count, const CancelCallback& should_cancel) {
    const auto cancelled = [&]() { return should_cancel && should_cancel(); };

    PKG pkg;
    if (const auto passcode = PkgPasscode::LookupForFile(pkg_file)) {
        pkg.SetPasscode(*passcode);
    }
    ReportProgress(progress,
                   {.pkg_index = pkg_index,
                    .pkg_count = pkg_count,
                    .stage = "opening",
                    .pkg_path = pkg_file});

    if (cancelled()) {
        failreason = "Installation cancelled.";
        return false;
    }

    if (!pkg.Open(pkg_file, failreason)) {
        return false;
    }

    ReportProgress(progress,
                   {.pkg_index = pkg_index,
                    .pkg_count = pkg_count,
                    .stage = "metadata",
                    .pkg_path = pkg_file});

    if (cancelled()) {
        failreason = "Installation cancelled.";
        return false;
    }

    if (!pkg.Extract(pkg_file, extract_path, failreason)) {
        return false;
    }

    if (cancelled()) {
        failreason = "Installation cancelled.";
        return false;
    }

    const int file_count = static_cast<int>(pkg.GetNumberOfFiles());
    int last_reported_file = -1;
    int last_reported_percent = -1;

    pkg.SetFileProgressCallback([&](int file_index, int block_percent, std::string_view file_name) {
        if (cancelled()) {
            return;
        }
        if (file_index != last_reported_file ||
            block_percent >= last_reported_percent + 10 || block_percent == 100) {
            last_reported_file = file_index;
            last_reported_percent = block_percent;
            ReportProgress(progress,
                           {.pkg_index = pkg_index,
                            .pkg_count = pkg_count,
                            .file_index = file_index,
                            .file_count = file_count,
                            .file_percent = block_percent,
                            .stage = "files",
                            .file_name = std::string(file_name),
                            .pkg_path = pkg_file});
        }
    });

    for (int i = 0; i < file_count; ++i) {
        if (cancelled()) {
            failreason = "Installation cancelled.";
            return false;
        }

        ReportProgress(progress,
                       {.pkg_index = pkg_index,
                        .pkg_count = pkg_count,
                        .file_index = i,
                        .file_count = file_count,
                        .file_percent = 0,
                        .stage = "files",
                        .pkg_path = pkg_file});
        last_reported_file = i;
        last_reported_percent = 0;

        pkg.ExtractFiles(i);
    }

    if (cancelled()) {
        failreason = "Installation cancelled.";
        return false;
    }

    const auto eboot = extract_path / "eboot.bin";
    std::error_code ec;
    if (!std::filesystem::exists(eboot, ec) || std::filesystem::file_size(eboot, ec) == 0) {
        const std::string title_id(pkg.GetTitleID());
        if (auto found = Common::FS::FindGameByID(extract_path.parent_path(), title_id, 3)) {
            if (std::filesystem::file_size(*found, ec) > 0) {
                return true;
            }
        }
        failreason =
            "PKG metadata extracted, but eboot.bin is missing or empty. The install is incomplete.";
        return false;
    }

    return true;
}

} // namespace

bool InstallPkgFiles(HWND parent, const std::vector<std::filesystem::path>& pkg_files,
                     ProgressCallback progress, std::string* last_error,
                     CancelCallback should_cancel) {
    const auto cancelled = [&]() { return should_cancel && should_cancel(); };
    const auto paths = ShadConfig::LoadInstallPaths();
    if (!paths) {
        if (last_error) {
            *last_error =
                "Could not read game install directories from shadPS4 config.\n"
                "Configure them via Game -> Game Install Directory first.";
        }
        if (!progress) {
            MessageBoxW(parent,
                        L"Could not read game install directories from shadPS4 config.\n"
                        L"Configure them via Game -> Game Install Directory first.",
                        L"PKG Install", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    const int pkg_count = static_cast<int>(pkg_files.size());
    int installed = 0;

    for (int pkg_index = 0; pkg_index < pkg_count; ++pkg_index) {
        if (cancelled()) {
            if (last_error) {
                *last_error = "Installation cancelled.";
            }
            return false;
        }

        const auto& pkg_file = pkg_files[pkg_index];

        ReportProgress(progress,
                       {.pkg_index = pkg_index,
                        .pkg_count = pkg_count,
                        .stage = "routing",
                        .pkg_path = pkg_file});

        PkgRouter::RouteResult route;
        if (PkgRouter::ResolveInstallPath(pkg_file, paths->games_dir, paths->addons_dir, route,
                                          parent) != PkgRouter::OverwriteDecision::Proceed) {
            if (last_error) {
                *last_error = kInstallAbortedError;
            }
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(route.extract_path, ec);

        std::string failreason;
        if (!ExtractPkg(pkg_file, route.extract_path, failreason, progress, pkg_index, pkg_count,
                        should_cancel)) {
            std::filesystem::remove_all(route.extract_path, ec);
            if (last_error) {
                *last_error = PkgPasscode::IsRequiredError(failreason) ? PkgPasscode::kRequiredError
                                                                        : failreason;
            }
            if (!progress && failreason != "Installation cancelled.") {
                const auto& message = PkgPasscode::IsRequiredError(failreason)
                                          ? PkgPasscode::RequiredErrorMessage()
                                          : failreason;
                MessageBoxW(parent, ToWide(message).c_str(), L"PKG Error", MB_OK | MB_ICONERROR);
            }
            return false;
        }

        ++installed;
        ReportProgress(progress,
                       {.pkg_index = pkg_index,
                        .pkg_count = pkg_count,
                        .file_index = 1,
                        .file_count = 1,
                        .file_percent = 100,
                        .stage = "done",
                        .pkg_path = pkg_file,
                        .extract_path = route.extract_path});

    }

    if (installed > 0 && !progress) {
        MenuHook::TriggerRefreshGameList(parent);
    }

    return installed == pkg_count;
}

bool InstallPkgFile(HWND parent, const std::filesystem::path& pkg_file,
                    ProgressCallback progress, CancelCallback should_cancel) {
    std::string failreason;
    return InstallPkgFiles(parent, {pkg_file}, progress, &failreason, std::move(should_cancel));
}

void RunInstallGameDialog(HWND parent) {
    auto files = PickPkgFiles(parent);
    if (files.empty()) {
        return;
    }

    std::string prepare_error;
    const auto work_dir = files.front().parent_path();
    const auto install_files = PkgMerge::PrepareInstallPaths(files, work_dir, prepare_error);
    if (install_files.empty()) {
        if (std::string_view(prepare_error) == PkgMerge::StandaloneDeltaPkgMessage()) {
            PkgRouter::ShowDeltaPkgNotSupported(parent);
        } else {
            MessageBoxW(parent, ToWide(prepare_error).c_str(), L"PKG Install",
                        MB_OK | MB_ICONWARNING);
        }
        return;
    }

    PkgInstallDialog::Run(install_files, parent);
}

void RunInstallDialog(HWND parent) {
    RunInstallGameDialog(parent);
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
        PkgInstallDialog::Run(files, parent);
    }
}

} // namespace PkgInstaller
