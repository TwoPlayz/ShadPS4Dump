#include "orbispatches/patch_link_fetcher.h"

#include "orbispatches/orbispatches_common.h"
#include "orbispatches/patch_http_download.h"
#include "plugin_module.h"

#include <nlohmann/json.hpp>
#include <objbase.h>
#include <sstream>
#include <wrl.h>

#ifndef interface
#define interface struct
#endif
#include <WebView2.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace OrbisPatches {

namespace {

constexpr UINT kFetchCompleteMsg = WM_APP + 42;

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

std::string FormatHResult(HRESULT hr) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return ss.str();
}

std::string WebView2RuntimeError() {
    return "Microsoft Edge WebView2 Runtime is not installed. "
           "Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703 then restart the "
           "launcher.";
}

bool EnsureWebView2Runtime(std::string& error) {
    LPWSTR version = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (SUCCEEDED(hr) && version) {
        CoTaskMemFree(version);
        return true;
    }
    error = WebView2RuntimeError();
    return false;
}

struct ComApartment {
    bool owns = false;
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hr == S_OK || hr == S_FALSE) {
            owns = (hr == S_OK);
        }
    }
    ~ComApartment() {
        if (owns) {
            CoUninitialize();
        }
    }
};

std::string EscapeForJavaScript(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '\'':
            out += "\\'";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

struct FetchContext {
    HWND hwnd = nullptr;
    bool finished = false;
    bool success = false;
    std::string response_json;
    std::string error;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    std::string titleid;
    std::string contentver;
    std::string patch_key;
};

FetchContext* GetContext(HWND hwnd) {
    return reinterpret_cast<FetchContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void FinishFetch(FetchContext* ctx, bool success, std::string message = {}) {
    if (!ctx || ctx->finished) {
        return;
    }
    ctx->finished = true;
    ctx->success = success;
    if (!message.empty()) {
        ctx->error = std::move(message);
    }
    if (ctx->hwnd) {
        PostMessageW(ctx->hwnd, kFetchCompleteMsg, 0, 0);
    }
}

void RunFetchScript(FetchContext* ctx) {
    if (!ctx || !ctx->webview) {
        return;
    }

    const std::string script = std::string("(async function() {"
                                           "try {"
                                           "await new Promise(function(resolve) { grecaptcha.ready(resolve); });"
                                           "const token = await grecaptcha.execute("
                                           "orbispatches.extServices.recaptchaKey, { action: 'patch' });"
                                           "const request = await httpPost(orbispatches.apiUri + '/patch', {"
                                           "titleid: '") +
                               EscapeForJavaScript(ctx->titleid) + "', contentver: '" +
                               EscapeForJavaScript(ctx->contentver) + "', key: '" +
                               EscapeForJavaScript(ctx->patch_key) +
                               "', token: token });"
                               "window.chrome.webview.postMessage(JSON.stringify(request));"
                               "} catch (err) {"
                               "window.chrome.webview.postMessage(JSON.stringify({"
                               "success: false, message: String(err)"
                               "}));"
                               "}"
                               "})();";

    ctx->webview->ExecuteScript(Utf8ToWide(script).c_str(), nullptr);
}

struct DownloadSessionContext;
void RunFetchScriptForSession(DownloadSessionContext* ctx);

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == kFetchCompleteMsg) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool ParsePiecesFromResponse(const std::string& response_json, std::vector<PatchPiece>& pieces,
                             std::string& error) {
    try {
        const auto json = nlohmann::json::parse(response_json);
        if (!json.value("success", false)) {
            error = json.value("message", "ORBISPatches rejected the patch request");
            return false;
        }
        if (!json.contains("pieces") || !json["pieces"].is_array()) {
            error = "ORBISPatches returned no patch pieces";
            return false;
        }

        for (const auto& item : json["pieces"]) {
            PatchPiece piece;
            piece.order = item.value("order", 0);
            piece.size_label = item.value("size", "");
            piece.hash = item.value("hash", "");
            piece.pkg_url = item.value("pkg_url", "");
            if (!piece.pkg_url.empty()) {
                pieces.push_back(std::move(piece));
            }
        }

        std::sort(pieces.begin(), pieces.end(),
                  [](const PatchPiece& a, const PatchPiece& b) { return a.order < b.order; });

        if (pieces.empty()) {
            error = "No full patch PKG pieces are available for this version.";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace

bool PatchLinkFetcher::FetchPieces(const std::string& titleid, const std::string& contentver,
                                   const std::string& patch_key, std::vector<PatchPiece>& pieces,
                                   std::string& error, HWND parent_hwnd,
                                   const PumpEventsFn& pump_events) {
    pieces.clear();
    if (titleid.empty() || contentver.empty() || patch_key.empty()) {
        error = "Missing patch metadata";
        return false;
    }

    if (!EnsureWebView2Runtime(error)) {
        return false;
    }

    ComApartment com;
    const HMODULE module = PluginModule::Handle();

    static bool class_registered = false;
    static const wchar_t kClassName[] = L"ShadPS4OrbisPatchFetcher";
    if (!class_registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = module;
        wc.lpszClassName = kClassName;
        if (!RegisterClassW(&wc)) {
            error = "Failed to create hidden fetcher window";
            return false;
        }
        class_registered = true;
    }

    FetchContext ctx;
    ctx.titleid = titleid;
    ctx.contentver = contentver;
    ctx.patch_key = patch_key;
    ctx.hwnd = CreateWindowExW(WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 0, 0, parent_hwnd,
                               nullptr, module, nullptr);
    if (!ctx.hwnd) {
        error = "Failed to create hidden fetcher window";
        return false;
    }
    SetWindowLongPtrW(ctx.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    ShowWindow(ctx.hwnd, SW_HIDE);

    const std::wstring user_data = WebViewUserDataPath().wstring();
  std::error_code ec;
  std::filesystem::create_directories(user_data, ec);

    const std::wstring start_url = L"https://orbispatches.com/" + Utf8ToWide(titleid);

    const HRESULT env_hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&ctx, start_url](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                    FinishFetch(&ctx, false,
                                "Failed to create WebView2 environment (" + FormatHResult(result) +
                                    "). " + WebView2RuntimeError());
                    return result;
                }

                environment->CreateCoreWebView2Controller(
                    ctx.hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&ctx, start_url](HRESULT controller_result,
                                          ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result) || !controller) {
                                FinishFetch(&ctx, false, "Failed to create WebView2 controller");
                                return controller_result;
                            }

                            ctx.controller = controller;
                            controller->get_CoreWebView2(&ctx.webview);

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(ctx.webview->get_Settings(&settings)) && settings) {
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            }

                            ctx.webview->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [&ctx](ICoreWebView2* /*sender*/,
                                           ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        auto* state = GetContext(ctx.hwnd);
                                        if (!state) {
                                            return S_OK;
                                        }

                                        LPWSTR message = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&message)) ||
                                            !message) {
                                            FinishFetch(state, false,
                                                        "Could not read ORBISPatches response");
                                            return S_OK;
                                        }

                                        state->response_json = WideToUtf8(message);
                                        CoTaskMemFree(message);
                                        FinishFetch(state, true);
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            Microsoft::WRL::ComPtr<ICoreWebView2NavigationCompletedEventHandler>
                                nav_handler = Microsoft::WRL::Callback<
                                    ICoreWebView2NavigationCompletedEventHandler>(
                                    [&ctx](ICoreWebView2* /*sender*/,
                                           ICoreWebView2NavigationCompletedEventArgs* args)
                                        -> HRESULT {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (!success) {
                                            FinishFetch(&ctx, false,
                                                        "Failed to load ORBISPatches page");
                                            return S_OK;
                                        }
                                        RunFetchScript(&ctx);
                                        return S_OK;
                                    });

                            ctx.webview->add_NavigationCompleted(nav_handler.Get(), nullptr);
                            ctx.webview->Navigate(start_url.c_str());
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(env_hr)) {
        DestroyWindow(ctx.hwnd);
        error = "Failed to start WebView2 (" + FormatHResult(env_hr) + "). " + WebView2RuntimeError();
        return false;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(120);
    MSG msg{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (ctx.finished) {
            break;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                ctx.finished = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (ctx.finished) {
            break;
        }
        if (pump_events) {
            pump_events();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (ctx.controller) {
        ctx.controller->Close();
    }
    DestroyWindow(ctx.hwnd);

    if (!ctx.finished) {
        error = "Timed out while contacting ORBISPatches";
        return false;
    }
    if (!ctx.success) {
        error = ctx.error.empty() ? "Failed to fetch patch download links" : ctx.error;
        return false;
    }

    return ParsePiecesFromResponse(ctx.response_json, pieces, error);
}

namespace {

std::filesystem::path PieceDestinationPath(const std::filesystem::path& directory,
                                           const PatchPiece& piece, size_t index) {
    const std::wstring filename =
        L"piece_" + std::to_wstring(piece.order > 0 ? piece.order : index + 1) + L".pkg";
    return directory / filename;
}

void RemovePartialFile(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

struct DownloadSessionContext {
    HWND hwnd = nullptr;
    bool finished = false;
    bool success = false;
    std::string error;
    std::string response_json;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    std::string titleid;
    std::string contentver;
    std::string patch_key;
    std::vector<PatchPiece> pieces;
};

DownloadSessionContext* GetDownloadContext(HWND hwnd) {
    return reinterpret_cast<DownloadSessionContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void RunFetchScriptForSession(DownloadSessionContext* ctx) {
    if (!ctx || !ctx->webview) {
        return;
    }

    const std::string script = std::string("(async function() {"
                                           "try {"
                                           "await new Promise(function(resolve) { grecaptcha.ready(resolve); });"
                                           "const token = await grecaptcha.execute("
                                           "orbispatches.extServices.recaptchaKey, { action: 'patch' });"
                                           "const request = await httpPost(orbispatches.apiUri + '/patch', {"
                                           "titleid: '") +
                               EscapeForJavaScript(ctx->titleid) + "', contentver: '" +
                               EscapeForJavaScript(ctx->contentver) + "', key: '" +
                               EscapeForJavaScript(ctx->patch_key) +
                               "', token: token });"
                               "window.chrome.webview.postMessage(JSON.stringify(request));"
                               "} catch (err) {"
                               "window.chrome.webview.postMessage(JSON.stringify({"
                               "success: false, message: String(err)"
                               "}));"
                               "}"
                               "})();";

    ctx->webview->ExecuteScript(Utf8ToWide(script).c_str(), nullptr);
}

void FinishDownloadSession(DownloadSessionContext* ctx, bool success, std::string message = {}) {
    if (!ctx || ctx->finished) {
        return;
    }
    ctx->finished = true;
    ctx->success = success;
    if (!message.empty()) {
        ctx->error = std::move(message);
    }
    if (ctx->hwnd) {
        PostMessageW(ctx->hwnd, kFetchCompleteMsg, 0, 0);
    }
}

} // namespace

bool PatchLinkFetcher::FetchAndDownloadPieces(
    const std::string& titleid, const std::string& contentver, const std::string& patch_key,
    const std::filesystem::path& download_dir, std::vector<std::filesystem::path>& downloaded_paths,
    const PieceProgressFn& progress, std::string& error, HWND parent_hwnd,
    const PumpEventsFn& pump_events, CancelCallback should_cancel) {
    downloaded_paths.clear();
    if (titleid.empty() || contentver.empty() || patch_key.empty()) {
        error = "Missing patch metadata";
        return false;
    }

    if (!EnsureWebView2Runtime(error)) {
        return false;
    }

    ComApartment com;
    const HMODULE module = PluginModule::Handle();

    static bool download_class_registered = false;
    static const wchar_t kDownloadClassName[] = L"ShadPS4OrbisPatchDownloadSession";
    if (!download_class_registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = module;
        wc.lpszClassName = kDownloadClassName;
        if (!RegisterClassW(&wc)) {
            error = "Failed to create hidden download session window";
            return false;
        }
        download_class_registered = true;
    }

    DownloadSessionContext ctx;
    ctx.titleid = titleid;
    ctx.contentver = contentver;
    ctx.patch_key = patch_key;
    ctx.hwnd = CreateWindowExW(WS_EX_NOACTIVATE, kDownloadClassName, L"", WS_POPUP, 0, 0, 0, 0,
                                 parent_hwnd, nullptr, module, nullptr);
    if (!ctx.hwnd) {
        error = "Failed to create hidden download session window";
        return false;
    }
    SetWindowLongPtrW(ctx.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    ShowWindow(ctx.hwnd, SW_HIDE);

    std::error_code ec;
    std::filesystem::create_directories(download_dir, ec);

    const std::wstring user_data = WebViewUserDataPath().wstring();
    std::filesystem::create_directories(user_data, ec);

    const std::wstring start_url = L"https://orbispatches.com/" + Utf8ToWide(titleid);

    const HRESULT env_hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&ctx, start_url](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                    FinishDownloadSession(
                        &ctx, false,
                        "Failed to create WebView2 environment (" + FormatHResult(result) + "). " +
                            WebView2RuntimeError());
                    return result;
                }

                environment->CreateCoreWebView2Controller(
                    ctx.hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&ctx, start_url](HRESULT controller_result,
                                          ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result) || !controller) {
                                FinishDownloadSession(&ctx, false,
                                                      "Failed to create WebView2 controller");
                                return controller_result;
                            }

                            ctx.controller = controller;
                            controller->get_CoreWebView2(&ctx.webview);

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(ctx.webview->get_Settings(&settings)) && settings) {
                                settings->put_IsWebMessageEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            }

                            ctx.webview->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [&ctx](ICoreWebView2* /*sender*/,
                                           ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        auto* state = GetDownloadContext(ctx.hwnd);
                                        if (!state || state->finished) {
                                            return S_OK;
                                        }

                                        LPWSTR message = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&message)) ||
                                            !message) {
                                            FinishDownloadSession(
                                                state, false, "Could not read ORBISPatches response");
                                            return S_OK;
                                        }

                                        state->response_json = WideToUtf8(message);
                                        CoTaskMemFree(message);

                                        if (!ParsePiecesFromResponse(state->response_json,
                                                                     state->pieces, state->error)) {
                                            FinishDownloadSession(state, false, state->error);
                                            return S_OK;
                                        }

                                        FinishDownloadSession(state, true);
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            ctx.webview->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [&ctx](ICoreWebView2* /*sender*/,
                                           ICoreWebView2NavigationCompletedEventArgs* args)
                                        -> HRESULT {
                                        if (ctx.finished) {
                                            return S_OK;
                                        }

                                        if (ctx.pieces.empty()) {
                                            BOOL success = FALSE;
                                            args->get_IsSuccess(&success);
                                            if (!success) {
                                                FinishDownloadSession(
                                                    &ctx, false, "Failed to load ORBISPatches page");
                                            } else {
                                                RunFetchScriptForSession(&ctx);
                                            }
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            ctx.webview->Navigate(start_url.c_str());
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(env_hr)) {
        DestroyWindow(ctx.hwnd);
        error = "Failed to start WebView2 (" + FormatHResult(env_hr) + "). " + WebView2RuntimeError();
        return false;
    }

    const auto fetch_deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    MSG msg{};
    while (std::chrono::steady_clock::now() < fetch_deadline) {
        if (ctx.finished) {
            break;
        }

        if (should_cancel && should_cancel()) {
            FinishDownloadSession(&ctx, false, "Download cancelled.");
        }

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                ctx.finished = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (ctx.finished) {
            break;
        }
        if (pump_events) {
            pump_events();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!ctx.finished) {
        if (ctx.controller) {
            ctx.controller->Close();
        }
        DestroyWindow(ctx.hwnd);
        error = "Timed out while contacting ORBISPatches";
        return false;
    }
    if (!ctx.success) {
        if (ctx.controller) {
            ctx.controller->Close();
        }
        DestroyWindow(ctx.hwnd);
        error = ctx.error.empty() ? "Failed to fetch patch download links" : ctx.error;
        return false;
    }

    std::string cookie_header;
    const std::string referer = "https://orbispatches.com/" + titleid;
    if (!ExportCookieHeader(ctx.webview.Get(), ctx.pieces.front().pkg_url, cookie_header, pump_events,
                            error)) {
        if (ctx.controller) {
            ctx.controller->Close();
        }
        DestroyWindow(ctx.hwnd);
        return false;
    }

    if (ctx.controller) {
        ctx.controller->Close();
    }
    DestroyWindow(ctx.hwnd);

    const auto download_deadline = std::chrono::steady_clock::now() + std::chrono::hours(6);
    for (size_t i = 0; i < ctx.pieces.size(); ++i) {
        if (should_cancel && should_cancel()) {
            error = "Download cancelled.";
            return false;
        }
        if (std::chrono::steady_clock::now() >= download_deadline) {
            error = "Timed out while downloading from ORBISPatches";
            return false;
        }

        const auto& piece = ctx.pieces[i];
        const auto destination = PieceDestinationPath(download_dir, piece, i);
        RemovePartialFile(destination);

        if (!DownloadUrlToFile(
                piece.pkg_url, destination, cookie_header, referer,
                [&](int64_t received, int64_t total) {
                    if (progress) {
                        progress(i, ctx.pieces.size(), received, total);
                    }
                    if (pump_events) {
                        pump_events();
                    }
                },
                should_cancel, error)) {
            RemovePartialFile(destination);
            return false;
        }

        if (!VerifyDownloadedPiece(piece, i, ctx.pieces.size(), destination, error)) {
            RemovePartialFile(destination);
            return false;
        }

        downloaded_paths.push_back(destination);
    }

    return true;
}

} // namespace OrbisPatches
