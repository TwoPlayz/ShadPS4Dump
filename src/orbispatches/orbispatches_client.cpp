#include "orbispatches/orbispatches_client.h"
#include "orbispatches/patch_webview_download.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include <regex>
#include <sstream>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace OrbisPatches {

namespace {

constexpr wchar_t kHost[] = L"orbispatches.com";
constexpr INTERNET_PORT kPort = INTERNET_DEFAULT_HTTPS_PORT;

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                            nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string UrlEncode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::ostringstream out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%' << hex[c >> 4] << hex[c & 0x0F];
        }
    }
    return out.str();
}

bool HttpRequest(const wchar_t* method, const std::wstring& path, const std::string& body,
                 std::string& response, std::string& error) {
    HINTERNET session =
        WinHttpOpen(L"ShadPS4PkgPlugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHttpOpen failed";
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, kHost, kPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect failed";
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, method, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest failed";
        return false;
    }

    DWORD security_flags =
        SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, sizeof(security_flags));

    LPCWSTR headers = L"Content-Type: application/json\r\nAccept: application/json";
    LPVOID body_ptr = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const BOOL ok = WinHttpSendRequest(
        request, headers, static_cast<DWORD>(-1L), body_ptr,
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!ok || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "HTTP request failed";
        return false;
    }

    response.clear();
    DWORD available = 0;
    do {
        if (!WinHttpQueryDataAvailable(request, &available)) {
            break;
        }
        if (available == 0) {
            break;
        }
        const size_t offset = response.size();
        response.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, response.data() + offset, available, &read)) {
            response.resize(offset);
            break;
        }
        response.resize(offset + read);
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

bool HttpGet(const std::wstring& path, std::string& response, std::string& error) {
    return HttpRequest(L"GET", path, {}, response, error);
}

bool HttpPostJson(const std::wstring& path, const std::string& body, std::string& response,
                  std::string& error) {
    return HttpRequest(L"POST", path, body, response, error);
}

std::optional<LoadParams> ParseLoadParamsFromHtml(const std::string& html) {
    static const std::regex pattern(
        R"(data-loadparams\s*=\s*"\{\s*'titleid'\s*:\s*'([^']+)'\s*,\s*'key'\s*:\s*'([^']+)'\s*\}")");
    std::smatch match;
    if (!std::regex_search(html, match, pattern)) {
        return std::nullopt;
    }
    LoadParams params;
    params.titleid = match[1].str();
    params.key = match[2].str();
    return params;
}

} // namespace

std::vector<SearchResult> Client::Search(const std::string& term, std::string& error) {
    std::vector<SearchResult> results;
    if (term.empty()) {
        return results;
    }

    const std::wstring path =
        L"/api/internal/search?term=" + Utf8ToWide(UrlEncode(term));
    std::string response;
    if (!HttpGet(path, response, error)) {
        return results;
    }

    try {
        const auto json = nlohmann::json::parse(response);
        if (!json.value("success", false)) {
            error = json.value("message", "Search failed");
            return results;
        }
        for (const auto& item : json.at("results")) {
            SearchResult row;
            row.titleid = item.value("titleid", "");
            row.name = item.value("name", "");
            row.region = item.value("region", "");
            row.icon_url = item.value("icon", "");
            if (!row.titleid.empty()) {
                results.push_back(std::move(row));
            }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    return results;
}

std::optional<LoadParams> Client::FetchLoadParams(const std::string& titleid, std::string& error) {
    const std::wstring path = L"/" + Utf8ToWide(titleid);
    std::string response;
    if (!HttpGet(path, response, error)) {
        return std::nullopt;
    }
    auto params = ParseLoadParamsFromHtml(response);
    if (!params) {
        error = "Could not read patch list key from ORBISPatches page";
    }
    return params;
}

std::vector<PatchEntry> Client::LoadPatches(const std::string& titleid, const std::string& key,
                                            std::string& error) {
    std::vector<PatchEntry> patches;
    nlohmann::json body = {{"titleid", titleid}, {"key", key}};
    std::string response;
    if (!HttpPostJson(L"/api/internal/loadpatches", body.dump(), response, error)) {
        return patches;
    }

    try {
        const auto json = nlohmann::json::parse(response);
        if (!json.value("success", false)) {
            error = json.value("message", "Failed to load patches");
            return patches;
        }
        for (const auto& item : json.at("patches")) {
            PatchEntry row;
            row.version = item.value("version", "");
            row.filesize = item.value("filesize", "");
            row.required_firmware = item.value("required_firmware", "");
            row.creation_date = item.value("creation_date", "");
            row.changelog_preview = item.value("changelog_preview", "");
            row.is_latest = item.value("is_latest", false);
            if (item.contains("keyset") && item["keyset"].contains("patch")) {
                row.patch_key = item["keyset"]["patch"].get<std::string>();
            }
            if (!row.version.empty()) {
                patches.push_back(std::move(row));
            }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    return patches;
}

bool Client::DownloadUrl(const std::string& url, const std::filesystem::path& destination,
                         const DownloadProgressFn& progress, std::string& error,
                         CancelCallback should_cancel, HWND parent_hwnd,
                         const PumpEventsFn& pump_events) {
    if (DownloadUrlViaWebView(url, destination, progress, error, should_cancel, parent_hwnd,
                              pump_events)) {
        return true;
    }
    if (error.empty()) {
        error = "Failed to download patch from ORBISPatches";
    }
    return false;
}

} // namespace OrbisPatches
