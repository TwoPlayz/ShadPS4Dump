#include "install/pkg_router.h"

#include <filesystem>
#include <sstream>
#include <windows.h>

#include "common/path_util.h"
#include "common/string_util.h"
#include "core/file_format/pkg.h"
#include "core/file_format/psf.h"
#include "core/loader.h"

namespace PkgRouter {

namespace {

int ShowYesNo(HWND parent, const std::wstring& title, const std::wstring& text,
              bool default_no = true) {
    return MessageBoxW(parent, text.c_str(), title.c_str(),
                       MB_YESNO | (default_no ? MB_DEFBUTTON2 : MB_DEFBUTTON1) | MB_ICONQUESTION);
}

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

} // namespace

OverwriteDecision ResolveInstallPath(const std::filesystem::path& pkg_file,
                                     const std::filesystem::path& games_dir,
                                     const std::filesystem::path& addons_dir, RouteResult& out,
                                     HWND parent) {
    if (Loader::DetectFileType(pkg_file) != Loader::FileTypes::Pkg) {
        MessageBoxW(parent, L"The selected file does not appear to be a valid PKG.",
                    L"PKG Error", MB_OK | MB_ICONERROR);
        return OverwriteDecision::Cancel;
    }

    PKG pkg;
    std::string failreason;
    if (!pkg.Open(pkg_file, failreason)) {
        MessageBoxW(parent, ToWide(failreason).c_str(), L"PKG Error", MB_OK | MB_ICONERROR);
        return OverwriteDecision::Cancel;
    }

    PSF psf;
    if (!psf.Open(pkg.sfo)) {
        MessageBoxW(parent, L"Could not read param.sfo from PKG.", L"PKG Error",
                    MB_OK | MB_ICONERROR);
        return OverwriteDecision::Cancel;
    }

    out.title_id = pkg.GetTitleID();
    const auto category = psf.GetString("CATEGORY");
    const std::string pkg_flags = pkg.GetPkgFlags();
    out.is_patch = pkg_flags.find("PATCH") != std::string::npos;
    out.is_dlc = category.has_value() && std::string{*category} == "ac";

    auto game_folder_path = games_dir / out.title_id;
    auto game_update_path = game_folder_path;

    constexpr int max_depth = 5;
    if (out.is_patch) {
        if (auto found = Common::FS::FindGameByID(games_dir, out.title_id, max_depth)) {
            game_folder_path = found->parent_path();
        }
        game_update_path = game_folder_path.parent_path() / (out.title_id + "-patch");
    } else if (!out.is_dlc) {
        if (auto found = Common::FS::FindGameByID(games_dir, out.title_id, max_depth)) {
            game_folder_path = found->parent_path();
        } else {
            game_folder_path = games_dir / out.title_id;
        }
        game_update_path = game_folder_path;
    }

    if (out.is_dlc) {
        const auto content_id = psf.GetString("CONTENT_ID");
        if (!content_id.has_value()) {
            MessageBoxW(parent, L"PSF file has no CONTENT_ID.", L"PKG Error", MB_OK | MB_ICONERROR);
            return OverwriteDecision::Cancel;
        }
        const auto parts = Common::SplitString(std::string{*content_id}, '-');
        if (parts.size() < 3) {
            MessageBoxW(parent, L"Invalid CONTENT_ID in PSF.", L"PKG Error", MB_OK | MB_ICONERROR);
            return OverwriteDecision::Cancel;
        }
        out.entitlement_label = parts[2];
        game_update_path = addons_dir / out.title_id / out.entitlement_label;
    }

    out.extract_path = game_update_path;

    if (!std::filesystem::exists(out.extract_path)) {
        if (out.is_patch || out.is_dlc) {
            const wchar_t* msg = out.is_dlc
                                     ? L"DLC PKG detected, but the base game does not appear to be "
                                       L"installed.\n\n"
                                       L"You can install the base game first, then install this PKG "
                                       L"later via File -> Install Packages (PKG).\n\n"
                                       L"Continue installing now anyway?"
                                     : L"Patch PKG detected, but the base game does not appear to be "
                                       L"installed.\n\n"
                                       L"You can install the base game first, then install this patch "
                                       L"later via File -> Install Packages (PKG).\n\n"
                                       L"Continue installing the patch now anyway?";
            if (ShowYesNo(parent, L"PKG Extraction", msg) != IDYES) {
                return OverwriteDecision::Cancel;
            }
        }
        return OverwriteDecision::Proceed;
    }

    if (out.is_patch) {
        const auto pkg_ver = psf.GetString("APP_VER");
        if (!pkg_ver.has_value()) {
            MessageBoxW(parent, L"PSF file has no APP_VER.", L"PKG Error", MB_OK | MB_ICONERROR);
            return OverwriteDecision::Cancel;
        }

        std::filesystem::path sce_folder_path =
            std::filesystem::exists(game_update_path / "sce_sys" / "param.sfo")
                ? game_update_path / "sce_sys" / "param.sfo"
                : game_folder_path / "sce_sys" / "param.sfo";

        PSF installed_psf;
        if (!installed_psf.Open(sce_folder_path)) {
            MessageBoxW(parent, L"Could not read installed game version.", L"PKG Error",
                        MB_OK | MB_ICONERROR);
            return OverwriteDecision::Cancel;
        }

        const auto installed_ver = installed_psf.GetString("APP_VER");
        if (!installed_ver.has_value()) {
            MessageBoxW(parent, L"Installed game param.sfo has no APP_VER.", L"PKG Error",
                        MB_OK | MB_ICONERROR);
            return OverwriteDecision::Cancel;
        }

        std::wstringstream ss;
        ss << L"Patch detected.\nPKG version: " << ToWide(std::string{*pkg_ver})
           << L"\nInstalled version: " << ToWide(std::string{*installed_ver})
           << L"\n\nWould you like to overwrite?";
        if (ShowYesNo(parent, L"PKG Extraction", ss.str()) != IDYES) {
            return OverwriteDecision::Cancel;
        }
    } else if (out.is_dlc) {
        std::wstringstream ss;
        ss << L"DLC already installed at:\n" << out.extract_path.wstring()
           << L"\n\nWould you like to overwrite?";
        if (ShowYesNo(parent, L"DLC Installation", ss.str()) != IDYES) {
            return OverwriteDecision::Cancel;
        }
    } else {
        std::wstringstream ss;
        ss << L"Game already installed at:\n" << game_folder_path.wstring()
           << L"\n\nWould you like to overwrite?";
        if (ShowYesNo(parent, L"PKG Extraction", ss.str()) != IDYES) {
            return OverwriteDecision::Cancel;
        }
    }

    return OverwriteDecision::Proceed;
}

} // namespace PkgRouter
