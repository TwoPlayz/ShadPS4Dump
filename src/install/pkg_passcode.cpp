#include "install/pkg_passcode.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

#include "common/io_file.h"
#include "config/shad_config.h"
#include "core/file_format/pkg.h"

namespace PkgPasscode {

namespace {

std::filesystem::path PasscodeStorePath() {
    return ShadConfig::GetUserConfigDir() / "pkg_passcodes.json";
}

std::string NormalizePasscode(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char ch) { return std::isspace(ch) != 0; }),
                value.end());
    return value;
}

std::optional<std::string> ReadSidecarPasscode(const std::filesystem::path& directory) {
    static constexpr const wchar_t* kSidecarNames[] = {L"passcode.txt", L"passcode.zrf", L".passcode"};
    for (const wchar_t* name : kSidecarNames) {
        const auto sidecar = directory / name;
        std::ifstream in(sidecar);
        if (!in) {
            continue;
        }
        std::string passcode;
        in >> passcode;
        passcode = NormalizePasscode(std::move(passcode));
        if (passcode.size() == 32) {
            return passcode;
        }
    }
    return std::nullopt;
}

std::string ReadContentId(const std::filesystem::path& pkg_file) {
    Common::FS::IOFile file(pkg_file, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return {};
    }

    PKGHeader header{};
    file.Read(header);
    return std::string(reinterpret_cast<const char*>(header.pkg_content_id),
                       strnlen(reinterpret_cast<const char*>(header.pkg_content_id),
                               sizeof(header.pkg_content_id)));
}

std::optional<std::string> LookupInStore(const std::string& content_id) {
    if (content_id.empty()) {
        return std::nullopt;
    }

    const auto store_path = PasscodeStorePath();
    std::ifstream in(store_path);
    if (!in) {
        return std::nullopt;
    }

    try {
        nlohmann::json json;
        in >> json;
        if (!json.is_object() || !json.contains(content_id) || !json[content_id].is_string()) {
            return std::nullopt;
        }
        const auto passcode = NormalizePasscode(json[content_id].get<std::string>());
        if (passcode.size() == 32) {
            return passcode;
        }
    } catch (...) {
    }
    return std::nullopt;
}

} // namespace

const char* RequiredErrorMessage() {
    return "This is a retail (official Sony) PKG and needs its 32-character PKG passcode to "
           "decrypt.\n\n"
           "ORBISPatches downloads are retail patch PKGs — the passcode is not included in the "
           "download. Use the same passcode you would enter in LibOrbisPkg PkgTool "
           "(--passcode).\n\n"
           "Save the passcode as passcode.txt next to the PKG, or enter it when prompted.\n\n"
           "If your installed base game is a fake/homebrew PKG dump, you need a matching fake "
           "patch PKG instead of the official ORBISPatches download.";
}

bool IsRequiredError(std::string_view error) {
    return error == kRequiredError;
}

std::optional<std::string> LookupForFile(const std::filesystem::path& pkg_file) {
    if (const auto sidecar = ReadSidecarPasscode(pkg_file.parent_path())) {
        return sidecar;
    }
    return LookupInStore(ReadContentId(pkg_file));
}

std::string ContentIdForFile(const std::filesystem::path& pkg_file) {
    return ReadContentId(pkg_file);
}

void SaveForContentId(const std::string& content_id, const std::string& passcode) {
    const auto normalized = NormalizePasscode(passcode);
    if (content_id.empty() || normalized.size() != 32) {
        return;
    }

    nlohmann::json json = nlohmann::json::object();
    const auto store_path = PasscodeStorePath();
    std::ifstream in(store_path);
    if (in) {
        try {
            in >> json;
            if (!json.is_object()) {
                json = nlohmann::json::object();
            }
        } catch (...) {
            json = nlohmann::json::object();
        }
    }

    json[content_id] = normalized;
    std::error_code ec;
    std::filesystem::create_directories(store_path.parent_path(), ec);
    std::ofstream out(store_path, std::ios::trunc);
    if (out) {
        out << json.dump(2);
    }
}

} // namespace PkgPasscode
