#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace PkgPasscode {

constexpr const char* kRequiredError =
    "RETAIL_PKG_PASSCODE_REQUIRED";

const char* RequiredErrorMessage();

bool IsRequiredError(std::string_view error);

std::optional<std::string> LookupForFile(const std::filesystem::path& pkg_file);

std::string ContentIdForFile(const std::filesystem::path& pkg_file);

void SaveForContentId(const std::string& content_id, const std::string& passcode);

} // namespace PkgPasscode
