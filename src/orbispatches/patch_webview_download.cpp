#include "orbispatches/patch_webview_download.h"

#include "orbispatches/orbispatches_common.h"
#include "plugin_module.h"

#ifndef interface
#define interface struct
#endif
#include <WebView2.h>
#include <objbase.h>
#include <wrl.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>

namespace OrbisPatches {

namespace {

constexpr UINT kDownloadCompleteMsg = WM_APP + 43;

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

struct DownloadContext {
    HWND hwnd = nullptr;
    bool finished = false;
    bool success = false;
    bool download_started = false;
    enum class Phase { Warmup, Downloading } phase = Phase::Warmup;
    std::chrono::steady_clock::time_point navigation_completed_at{};
    std::string error;
    std::string url;
    std::filesystem::path destination;
    DownloadProgressFn progress;
    CancelCallback should_cancel;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> download;
};

DownloadContext* GetContext(HWND hwnd) {
    return reinterpret_cast<DownloadContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void FinishDownload(DownloadContext* ctx, bool success, std::string message = {}) {
    if (!ctx || ctx->finished) {
        return;
    }
    ctx->finished = true;
    ctx->success = success;
    if (!message.empty()) {
        ctx->error = std::move(message);
    }
    if (ctx->hwnd) {
        PostMessageW(ctx->hwnd, kDownloadCompleteMsg, 0, 0);
    }
}

void RemovePartialFile(const std::filesystem::path& destination) {
    std::error_code ec;
    std::filesystem::remove(destination, ec);
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == kDownloadCompleteMsg) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void HandleDownloadStarting(DownloadContext* ctx,
                            ICoreWebView2DownloadStartingEventArgs* args) {
    if (!ctx || !args) {
        return;
    }

    ctx->download_started = true;
    args->put_Handled(TRUE);
    args->put_ResultFilePath(ctx->destination.c_str());

    Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> operation;
    if (FAILED(args->get_DownloadOperation(&operation)) || !operation) {
        FinishDownload(ctx, false, "Could not start ORBISPatches download");
        return;
    }

    ctx->download = operation;

    operation->add_BytesReceivedChanged(
        Microsoft::WRL::Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
            [ctx](ICoreWebView2DownloadOperation* op, IUnknown* /*sender*/) -> HRESULT {
                if (!ctx || !ctx->progress || !op) {
                    return S_OK;
                }
                INT64 received = 0;
                INT64 total = 0;
                op->get_BytesReceived(&received);
                op->get_TotalBytesToReceive(&total);
                ctx->progress(received, total);
                return S_OK;
            })
            .Get(),
        nullptr);

    operation->add_StateChanged(
        Microsoft::WRL::Callback<ICoreWebView2StateChangedEventHandler>(
            [ctx](ICoreWebView2DownloadOperation* op, IUnknown* /*sender*/) -> HRESULT {
                if (!ctx || !op) {
                    return S_OK;
                }

                COREWEBVIEW2_DOWNLOAD_STATE state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
                op->get_State(&state);
                if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
                    FinishDownload(ctx, true);
                } else if (state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
                    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason =
                        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
                    op->get_InterruptReason(&reason);
                    RemovePartialFile(ctx->destination);
                    if (reason == COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED) {
                        FinishDownload(ctx, false, "Download cancelled.");
                    } else {
                        FinishDownload(ctx, false,
                                       "ORBISPatches download interrupted (reason " +
                                           std::to_string(static_cast<int>(reason)) + ")");
                    }
                }
                return S_OK;
            })
            .Get(),
        nullptr);
}

bool EnsureWebView2Runtime(std::string& error) {
    LPWSTR version = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    if (SUCCEEDED(hr) && version) {
        CoTaskMemFree(version);
        return true;
    }
    error = "Microsoft Edge WebView2 Runtime is not installed. "
            "Install it from https://go.microsoft.com/fwlink/p/?LinkId=2124703 then restart the "
            "launcher.";
    return false;
}

} // namespace

bool DownloadUrlViaWebView(const std::string& url, const std::filesystem::path& destination,
                           const DownloadProgressFn& progress, std::string& error,
                           CancelCallback should_cancel, HWND parent_hwnd,
                           const PumpEventsFn& pump_events) {
    if (url.empty()) {
        error = "Missing download URL";
        return false;
    }

    if (!EnsureWebView2Runtime(error)) {
        return false;
    }

    ComApartment com;
    const HMODULE module = PluginModule::Handle();

    static bool class_registered = false;
    static const wchar_t kClassName[] = L"ShadPS4OrbisPatchDownloader";
    if (!class_registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = HiddenWndProc;
        wc.hInstance = module;
        wc.lpszClassName = kClassName;
        if (!RegisterClassW(&wc)) {
            error = "Failed to create hidden downloader window";
            return false;
        }
        class_registered = true;
    }

    DownloadContext ctx;
    ctx.url = url;
    ctx.destination = destination;
    ctx.progress = progress;
    ctx.should_cancel = std::move(should_cancel);
    ctx.hwnd = CreateWindowExW(WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP, 0, 0, 0, 0,
                                 parent_hwnd, nullptr, module, nullptr);
    if (!ctx.hwnd) {
        error = "Failed to create hidden downloader window";
        return false;
    }
    SetWindowLongPtrW(ctx.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    ShowWindow(ctx.hwnd, SW_HIDE);

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    RemovePartialFile(destination);

    const std::wstring user_data = WebViewUserDataPath().wstring();

    const HRESULT env_hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&ctx](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                    FinishDownload(&ctx, false,
                                   "Failed to create WebView2 environment (" +
                                       FormatHResult(result) + ")");
                    return result;
                }

                environment->CreateCoreWebView2Controller(
                    ctx.hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&ctx](HRESULT controller_result,
                               ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result) || !controller) {
                                FinishDownload(&ctx, false, "Failed to create WebView2 controller");
                                return controller_result;
                            }

                            ctx.controller = controller;
                            controller->get_CoreWebView2(&ctx.webview);

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(ctx.webview->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                            }

                            Microsoft::WRL::ComPtr<ICoreWebView2_4> webview4;
                            if (FAILED(ctx.webview.As(&webview4)) || !webview4) {
                                FinishDownload(&ctx, false, "WebView2 download API unavailable");
                                return E_FAIL;
                            }

                            webview4->add_DownloadStarting(
                                Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                    [&ctx](ICoreWebView2* /*sender*/,
                                           ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                        HandleDownloadStarting(&ctx, args);
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            ctx.webview->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [&ctx](ICoreWebView2* sender,
                                           ICoreWebView2NavigationCompletedEventArgs* args)
                                        -> HRESULT {
                                        if (ctx.finished || ctx.download_started) {
                                            return S_OK;
                                        }

                                        ctx.navigation_completed_at = std::chrono::steady_clock::now();

                                        if (ctx.phase == DownloadContext::Phase::Warmup) {
                                            ctx.phase = DownloadContext::Phase::Downloading;
                                            ctx.navigation_completed_at = {};
                                            sender->Navigate(Utf8ToWide(ctx.url).c_str());
                                            return S_OK;
                                        }

                                        // Navigations that become file downloads report
                                        // IsSuccess=false; DownloadStarting handles them.
                                        return S_OK;
                                    })
                                    .Get(),
                                nullptr);

                            ctx.webview->Navigate(L"https://orbispatches.com/");
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(env_hr)) {
        DestroyWindow(ctx.hwnd);
        error = "Failed to start WebView2 (" + FormatHResult(env_hr) + ")";
        return false;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::hours(6);
    MSG msg{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (ctx.finished) {
            break;
        }

        if (ctx.should_cancel && ctx.should_cancel()) {
            if (ctx.download) {
                ctx.download->Cancel();
            } else {
                RemovePartialFile(destination);
                FinishDownload(&ctx, false, "Download cancelled.");
            }
        }

        if (!ctx.finished && ctx.phase == DownloadContext::Phase::Downloading &&
            !ctx.download_started &&
            ctx.navigation_completed_at != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - ctx.navigation_completed_at >
                std::chrono::seconds(15)) {
            RemovePartialFile(destination);
            FinishDownload(&ctx, false,
                             "ORBISPatches did not start downloading the patch file. Delete the "
                             "patch folder and try again.");
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
        RemovePartialFile(destination);
        error = "Timed out while downloading from ORBISPatches";
        return false;
    }

    if (!ctx.success) {
        error = ctx.error.empty() ? "Failed to download patch from ORBISPatches" : ctx.error;
        return false;
    }

    std::error_code size_ec;
    const auto file_size = std::filesystem::file_size(destination, size_ec);
    if (size_ec || file_size == 0) {
        RemovePartialFile(destination);
        error = "Downloaded file is empty";
        return false;
    }

    return true;
}

} // namespace OrbisPatches
