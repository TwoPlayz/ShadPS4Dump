#include "orbispatches/patch_http_download.h"

#include "install/pkg_merge.h"
#include "orbispatches/patch_link_fetcher.h"

#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/sha.h>

#ifndef interface
#define interface struct
#endif
#include <WebView2.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace OrbisPatches {

namespace {

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

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string FormatHumanBytes(uint64_t bytes) {
    std::ostringstream out;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        out << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024ULL * 1024ULL) {
        out << (bytes / (1024.0 * 1024.0)) << " MB";
    } else {
        out << bytes << " bytes";
    }
    return out.str();
}

bool CrackHttpUrl(const std::string& url, URL_COMPONENTS& parts, std::wstring& url_buffer) {
    url_buffer = Utf8ToWide(url);
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    return WinHttpCrackUrl(url_buffer.c_str(), static_cast<DWORD>(url_buffer.size()), 0, &parts) ==
           TRUE;
}

std::wstring HostFromParts(const URL_COMPONENTS& parts, const std::wstring& url_buffer) {
    return url_buffer.substr(parts.lpszHostName - url_buffer.c_str(), parts.dwHostNameLength);
}

std::wstring PathFromParts(const URL_COMPONENTS& parts, const std::wstring& url_buffer) {
    std::wstring path =
        url_buffer.substr(parts.lpszUrlPath - url_buffer.c_str(), parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        path += url_buffer.substr(parts.lpszExtraInfo - url_buffer.c_str(), parts.dwExtraInfoLength);
    }
    return path;
}

INTERNET_PORT PortFromParts(const URL_COMPONENTS& parts) {
    if (parts.nPort != 0) {
        return parts.nPort;
    }
    return parts.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT
                                                  : INTERNET_DEFAULT_HTTP_PORT;
}

bool QueryContentLength(HINTERNET request, int64_t& content_length) {
    content_length = -1;
    DWORD size = sizeof(content_length);
    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                            WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
        return true;
    }

    wchar_t buffer[64]{};
    size = sizeof(buffer);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
        try {
            content_length = std::stoll(buffer);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::optional<std::string> Sha256HexFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    CryptoPP::SHA256 sha;
    std::array<CryptoPP::byte, 4096> buffer{};
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = in.gcount();
        if (read <= 0) {
            break;
        }
        sha.Update(buffer.data(), static_cast<size_t>(read));
    }

    std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest{};
    sha.Final(digest.data());

    std::string hex;
    CryptoPP::HexEncoder encoder(new CryptoPP::StringSink(hex), false);
    encoder.Put(digest.data(), digest.size());
    encoder.MessageEnd();
    return ToLowerAscii(hex);
}

void AppendUniqueCookie(std::string& header, const std::string& name, const std::string& value) {
    const std::string needle = name + "=";
    if (header.find(needle) != std::string::npos) {
        return;
    }
    if (!header.empty()) {
        header += "; ";
    }
    header += name + "=" + value;
}

} // namespace

bool ExportCookieHeader(ICoreWebView2* webview, const std::string& url, std::string& cookie_header,
                        const PumpEventsFn& pump_events, std::string& error) {
    cookie_header.clear();
    if (!webview || url.empty()) {
        error = "Missing WebView session for ORBISPatches cookies";
        return false;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2_2> webview2;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&webview2))) || !webview2) {
        error = "WebView2 cookie API unavailable";
        return false;
    }

    Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookie_manager;
    if (FAILED(webview2->get_CookieManager(&cookie_manager)) || !cookie_manager) {
        error = "WebView2 cookie manager unavailable";
        return false;
    }

    struct CookieFetchState {
        bool done = false;
        HRESULT hr = E_FAIL;
        std::string header;
    };

    const auto merge_cookies = [&](const std::string& source) {
        std::istringstream stream(source);
        std::string item;
        while (std::getline(stream, item, ';')) {
            const auto start = item.find_first_not_of(' ');
            if (start == std::string::npos) {
                continue;
            }
            item = item.substr(start);
            const auto eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            AppendUniqueCookie(cookie_header, item.substr(0, eq), item.substr(eq + 1));
        }
    };

    const auto fetch_for_url = [&](const std::wstring& cookie_url) {
        CookieFetchState state;
        cookie_manager->GetCookies(
            cookie_url.c_str(),
            Microsoft::WRL::Callback<ICoreWebView2GetCookiesCompletedHandler>(
                [&state](HRESULT hr, ICoreWebView2CookieList* list) -> HRESULT {
                    state.hr = hr;
                    if (SUCCEEDED(hr) && list) {
                        UINT count = 0;
                        list->get_Count(&count);
                        for (UINT i = 0; i < count; ++i) {
                            Microsoft::WRL::ComPtr<ICoreWebView2Cookie> cookie;
                            if (FAILED(list->GetValueAtIndex(i, &cookie)) || !cookie) {
                                continue;
                            }
                            LPWSTR name = nullptr;
                            LPWSTR value = nullptr;
                            cookie->get_Name(&name);
                            cookie->get_Value(&value);
                            if (name && value) {
                                AppendUniqueCookie(state.header, WideToUtf8(name), WideToUtf8(value));
                            }
                            if (name) {
                                CoTaskMemFree(name);
                            }
                            if (value) {
                                CoTaskMemFree(value);
                            }
                        }
                    }
                    state.done = true;
                    return S_OK;
                })
                .Get());

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!state.done && std::chrono::steady_clock::now() < deadline) {
            if (pump_events) {
                pump_events();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!state.done || FAILED(state.hr)) {
            return false;
        }
        merge_cookies(state.header);
        return true;
    };

    const std::wstring wide_url = Utf8ToWide(url);
    URL_COMPONENTS parts{};
    std::wstring url_buffer;
    std::wstring origin = L"https://orbispatches.com/";
    if (CrackHttpUrl(url, parts, url_buffer)) {
        origin = parts.nScheme == INTERNET_SCHEME_HTTPS ? L"https://" : L"http://";
        origin += HostFromParts(parts, url_buffer);
        origin += L"/";
    }

    fetch_for_url(L"https://orbispatches.com/");
    fetch_for_url(origin);
    fetch_for_url(wide_url);

    if (cookie_header.empty()) {
        error = "Could not read ORBISPatches session cookies";
        return false;
    }
    return true;
}

bool DownloadUrlToFile(const std::string& url, const std::filesystem::path& destination,
                       const std::string& cookie_header, const std::string& referer,
                       const HttpDownloadProgressFn& progress, const HttpCancelFn& should_cancel,
                       std::string& error) {
    if (url.empty()) {
        error = "Missing download URL";
        return false;
    }

    URL_COMPONENTS parts{};
    std::wstring url_buffer;
    if (!CrackHttpUrl(url, parts, url_buffer)) {
        error = "Invalid patch download URL";
        return false;
    }

    const std::wstring host = HostFromParts(parts, url_buffer);
    const std::wstring path = PathFromParts(parts, url_buffer);
    const INTERNET_PORT port = PortFromParts(parts);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);

    HINTERNET session =
        WinHttpOpen(L"ShadPS4PkgPlugin/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHttpOpen failed";
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect failed";
        return false;
    }

    HINTERNET request =
        WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest failed";
        return false;
    }

    if (secure) {
        DWORD security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                               SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                               SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags,
                         sizeof(security_flags));
    }

    std::wstring headers;
    if (!cookie_header.empty()) {
        headers += L"Cookie: " + Utf8ToWide(cookie_header) + L"\r\n";
    }
    if (!referer.empty()) {
        headers += L"Referer: " + Utf8ToWide(referer) + L"\r\n";
    }

    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "ORBISPatches download request failed";
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                            WINHTTP_NO_HEADER_INDEX) &&
        status_code != 200) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "ORBISPatches download returned HTTP " + std::to_string(status_code);
        return false;
    }

    int64_t content_length = -1;
    QueryContentLength(request, content_length);
    if (progress) {
        progress(0, content_length);
    }

    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "Could not create patch download file";
        return false;
    }

    int64_t received = 0;
    std::array<char, 1024 * 1024> buffer{};
    while (true) {
        if (should_cancel && should_cancel()) {
            out.close();
            std::filesystem::remove(destination, ec);
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            error = "Download cancelled.";
            return false;
        }

        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = "ORBISPatches download interrupted";
            break;
        }
        if (available == 0) {
            break;
        }

        DWORD read = 0;
        const DWORD chunk = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        if (!WinHttpReadData(request, buffer.data(), chunk, &read) || read == 0) {
            error = "ORBISPatches download interrupted while reading data";
            break;
        }

        out.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!out) {
            error = "Failed while writing patch download file";
            break;
        }

        received += static_cast<int64_t>(read);
        if (progress) {
            progress(received, content_length);
        }
    }

    out.close();
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!error.empty()) {
        std::filesystem::remove(destination, ec);
        return false;
    }
    return true;
}

std::optional<uint64_t> ParsePieceSizeLabel(const std::string& size_label) {
    if (size_label.empty()) {
        return std::nullopt;
    }

    std::istringstream stream(size_label);
    double value = 0.0;
    std::string unit;
    if (!(stream >> value >> unit)) {
        return std::nullopt;
    }

    unit = ToLowerAscii(unit);
    if (unit == "gb" || unit == "gib") {
        return static_cast<uint64_t>(value * 1024.0 * 1024.0 * 1024.0);
    }
    if (unit == "mb" || unit == "mib") {
        return static_cast<uint64_t>(value * 1024.0 * 1024.0);
    }
    if (unit == "kb" || unit == "kib") {
        return static_cast<uint64_t>(value * 1024.0);
    }
    if (unit == "b" || unit == "bytes") {
        return static_cast<uint64_t>(value);
    }
    return std::nullopt;
}

bool VerifyDownloadedPiece(const PatchPiece& piece, size_t piece_index, size_t piece_count,
                           const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Could not read downloaded patch piece " + std::to_string(piece_index + 1);
        return false;
    }

    const auto piece_label = std::to_string(piece_index + 1);

    if (piece_index == 0 && piece_count > 1 && !PkgMerge::HasPkgMagic(path)) {
        error = "Piece 1 download is corrupt (missing PKG header).";
        if (file_size == 4294967296ULL) {
            error += " The file stopped at exactly 4 GB, which usually means the download was "
                     "truncated.";
        }
        error += " Delete the patch folder and download again from ORBISPatches.";
        return false;
    }

    if (piece_index == 0 && piece_count == 1 && !PkgMerge::HasPkgMagic(path)) {
        error = PkgMerge::InvalidDownloadMessage(path);
        return false;
    }

    if (const auto expected_size = ParsePieceSizeLabel(piece.size_label)) {
        const uint64_t expected = *expected_size;
        const uint64_t delta =
            expected > file_size ? expected - file_size : file_size - expected;
        const uint64_t tolerance = std::max<uint64_t>(expected / 100, 1024 * 1024);
        if (delta > tolerance) {
            error = "Piece " + piece_label + " size mismatch (expected " +
                    piece.size_label + ", got " + FormatHumanBytes(file_size) +
                    "). Delete the patch folder and download again from ORBISPatches.";
            return false;
        }
    }

    if (!piece.hash.empty()) {
        const auto digest = Sha256HexFile(path);
        if (!digest) {
            error = "Could not verify piece " + piece_label + " hash";
            return false;
        }
        if (ToLowerAscii(*digest) != ToLowerAscii(piece.hash)) {
            error = "Piece " + piece_label +
                    " failed hash verification. The download is corrupt or incomplete. Delete "
                    "the patch folder and download again from ORBISPatches.";
            return false;
        }
    }

    return true;
}

} // namespace OrbisPatches
