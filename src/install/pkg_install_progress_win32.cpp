#include "install/pkg_install_progress_win32.h"

#include <algorithm>
#include <commctrl.h>
#include <filesystem>
#include <string>

#include "hook/menu_hook.h"
#include "install/pkg_installer.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,                                                                               \
                "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' "        \
                "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "    \
                "language='*'\"")

namespace PkgInstallProgressWin32 {

namespace {

constexpr int kProgressId = 1001;
constexpr int kStatusId = 1002;
constexpr int kTitleId = 1003;
constexpr int kPercentId = 1004;
constexpr int kCloseId = 1005;
constexpr COLORREF kBackground = RGB(32, 33, 36);
constexpr COLORREF kText = RGB(230, 230, 230);
constexpr wchar_t kWindowClass[] = L"ShadPS4PkgInstallProgress";

HBRUSH BackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(kBackground);
    return brush;
}

class Dialog {
public:
    explicit Dialog(HWND owner) : owner_(owner) {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
    }

    ~Dialog() {
        if (ui_font_) {
            DeleteObject(ui_font_);
        }
        if (title_font_) {
            DeleteObject(title_font_);
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (owner_) {
            EnableWindow(owner_, TRUE);
        }
    }

    Dialog(const Dialog&) = delete;
    Dialog& operator=(const Dialog&) = delete;

    void Show(int pkg_count) {
        pkg_count_ = pkg_count;
        if (!EnsureWindowClassRegistered()) {
            return;
        }

        const int width = 520;
        const int height = 230;
        RECT owner_rect{};
        if (owner_) {
            GetWindowRect(owner_, &owner_rect);
        } else {
            owner_rect.left = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
            owner_rect.top = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        }

        const int x = owner_rect.left + ((owner_rect.right - owner_rect.left - width) / 2);
        const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top - height) / 2);

        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kWindowClass,
            pkg_count == 1 ? L"Installing PKG" : L"Installing PKGs",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, owner_, nullptr,
            GetModuleHandleW(nullptr), this);

        if (!hwnd_) {
            return;
        }

        title_font_ = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        ui_font_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        title_hwnd_ = CreateWindowExW(0, L"STATIC", L"Installing package", WS_CHILD | WS_VISIBLE,
                                        20, 16, width - 40, 24, hwnd_,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTitleId)),
                                        GetModuleHandleW(nullptr), nullptr);

        status_hwnd_ = CreateWindowExW(
            0, L"STATIC", L"Preparing installation…", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 46,
            width - 100, 44, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)), GetModuleHandleW(nullptr),
            nullptr);

        percent_hwnd_ =
            CreateWindowExW(0, L"STATIC", L"0%", WS_CHILD | WS_VISIBLE | SS_RIGHT, width - 72, 46,
                            52, 20, hwnd_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPercentId)),
                            GetModuleHandleW(nullptr), nullptr);

        progress_hwnd_ = CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 20, 98, width - 40, 18,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProgressId)),
            GetModuleHandleW(nullptr), nullptr);

        close_hwnd_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | BS_PUSHBUTTON, width - 120,
                                      136, 100, 28, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseId)),
                                      GetModuleHandleW(nullptr), nullptr);

        SendMessageW(progress_hwnd_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(progress_hwnd_, PBM_SETMARQUEE, TRUE, 25);
        EnableWindow(close_hwnd_, FALSE);

        for (HWND child : {title_hwnd_, status_hwnd_, percent_hwnd_, close_hwnd_}) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
        }
        SendMessageW(title_hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(title_font_), TRUE);

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        PumpMessages();
    }

    void Update(const PkgInstaller::InstallProgress& state) {
        if (!hwnd_) {
            return;
        }

        const DWORD now = GetTickCount();
        const bool force = state.stage == "done" || state.file_percent == 0 || state.file_percent == 100;
        if (!force && now - last_paint_tick_ < 50) {
            return;
        }
        last_paint_tick_ = now;

        const std::wstring file_name = state.pkg_path.filename().wstring();
        std::wstring status;

        if (state.stage == "routing") {
            status = pkg_count_ == 1
                         ? L"Checking install location for " + file_name + L"…"
                         : L"Checking install location (PKG " +
                               std::to_wstring(state.pkg_index + 1) + L" of " +
                               std::to_wstring(state.pkg_count) + L")…";
        } else if (state.stage == "opening") {
            status = pkg_count_ == 1 ? L"Opening " + file_name + L"…"
                                     : L"Opening PKG " + std::to_wstring(state.pkg_index + 1) +
                                           L" of " + std::to_wstring(state.pkg_count) + L"…";
        } else if (state.stage == "metadata") {
            status = L"Reading package metadata…";
        } else if (state.stage == "files") {
            const std::wstring current_file =
                state.file_name.empty() ? L"files" : ToWide(state.file_name);
            status = L"Extracting " + current_file + L"  ·  " +
                     std::to_wstring(state.file_index + 1) + L" / " +
                     std::to_wstring(state.file_count);
        } else if (state.stage == "done") {
            status = L"Finished " + file_name;
        } else {
            status = L"Installing…";
        }

        SetWindowTextW(status_hwnd_, status.c_str());

        const int percent = OverallPercent(state);
        SetWindowTextW(percent_hwnd_, (std::to_wstring(percent) + L"%").c_str());

        if (state.stage == "opening" || state.stage == "metadata" || state.stage == "routing") {
            SendMessageW(progress_hwnd_, PBM_SETMARQUEE, TRUE, 25);
        } else {
            SendMessageW(progress_hwnd_, PBM_SETMARQUEE, FALSE, 0);
            SendMessageW(progress_hwnd_, PBM_SETPOS, percent, 0);
        }

        RedrawWindow(progress_hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        PumpMessages();
    }

    void SetFinished(bool ok, const std::wstring& message) {
        if (!hwnd_) {
            return;
        }

        finished_ = true;
        SendMessageW(progress_hwnd_, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(progress_hwnd_, PBM_SETPOS, ok ? 100 : 0, 0);
        SetWindowTextW(percent_hwnd_, ok ? L"100%" : L"0%");
        SetWindowTextW(status_hwnd_, message.c_str());
        SetWindowTextW(title_hwnd_, ok ? L"Install complete" : L"Install failed");
        SetWindowTextW(hwnd_, ok ? L"PKG installed" : L"PKG install failed");
        SetWindowTextW(close_hwnd_, L"Close");
        ShowWindow(close_hwnd_, SW_SHOW);
        EnableWindow(close_hwnd_, TRUE);
        PumpMessages();
    }

    void PumpMessages() {
        if (!hwnd_) {
            return;
        }

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void WaitUntilClosed() {
        while (hwnd_) {
            MSG msg{};
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        Dialog* dialog = nullptr;
        if (msg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            dialog = static_cast<Dialog*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
        } else {
            dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (dialog) {
            switch (msg) {
            case WM_COMMAND:
                if (LOWORD(wparam) == kCloseId && dialog->finished_) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                break;
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(wparam), kText);
                SetBkColor(reinterpret_cast<HDC>(wparam), kBackground);
                return reinterpret_cast<LRESULT>(BackgroundBrush());
            case WM_CTLCOLORBTN:
                SetTextColor(reinterpret_cast<HDC>(wparam), kText);
                SetBkColor(reinterpret_cast<HDC>(wparam), kBackground);
                return reinterpret_cast<LRESULT>(BackgroundBrush());
            case WM_CLOSE:
                if (dialog->finished_) {
                    DestroyWindow(hwnd);
                }
                return 0;
            case WM_DESTROY:
                dialog->hwnd_ = nullptr;
                if (dialog->owner_) {
                    EnableWindow(dialog->owner_, TRUE);
                    SetForegroundWindow(dialog->owner_);
                }
                return 0;
            default:
                break;
            }
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

private:
    static bool EnsureWindowClassRegistered() {
        static bool registered = false;
        if (registered) {
            return true;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = BackgroundBrush();
        wc.lpszClassName = kWindowClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        return registered;
    }

    static std::wstring ToWide(const std::string& text) {
        if (text.empty()) {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                             nullptr, 0);
        std::wstring out(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(),
                            size);
        return out;
    }

    static int OverallPercent(const PkgInstaller::InstallProgress& state) {
        if (state.pkg_count <= 0) {
            return 0;
        }

        const int pkg_weight = 100 / state.pkg_count;
        int pkg_progress = 0;

        if (state.stage == "opening" || state.stage == "metadata" || state.stage == "routing") {
            pkg_progress = pkg_weight / 10;
        } else if (state.stage == "files" && state.file_count > 0) {
            const int file_weight = std::max(1, pkg_weight / std::max(1, state.file_count));
            pkg_progress = (state.file_index * file_weight) +
                             ((state.file_percent * file_weight) / 100);
        } else if (state.stage == "done") {
            pkg_progress = pkg_weight;
        }

        return std::min(100, (state.pkg_index * pkg_weight) + pkg_progress);
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND title_hwnd_ = nullptr;
    HWND status_hwnd_ = nullptr;
    HWND percent_hwnd_ = nullptr;
    HWND progress_hwnd_ = nullptr;
    HWND close_hwnd_ = nullptr;
    HFONT title_font_ = nullptr;
    HFONT ui_font_ = nullptr;
    int pkg_count_ = 0;
    DWORD last_paint_tick_ = 0;
    bool finished_ = false;
};

} // namespace

std::wstring ToWideUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

void RunInstall(HWND owner, const std::vector<std::filesystem::path>& pkg_files) {
    if (pkg_files.empty()) {
        return;
    }

    Dialog dialog(owner);
    dialog.Show(static_cast<int>(pkg_files.size()));

    std::string error;
    const bool ok = PkgInstaller::InstallPkgFiles(
        owner, pkg_files,
        [&dialog](const PkgInstaller::InstallProgress& state) {
            dialog.Update(state);
            dialog.PumpMessages();
        },
        &error);

    if (ok) {
        dialog.SetFinished(true, pkg_files.size() == 1 ? L"PKG installed successfully."
                                                       : L"All PKGs installed successfully.");
    } else if (!error.empty()) {
        dialog.SetFinished(false, ToWideUtf8(error));
    } else {
        dialog.SetFinished(false, L"PKG installation did not complete.");
    }

    dialog.WaitUntilClosed();

    if (ok) {
        MenuHook::TriggerRefreshGameList(owner);
    }
}

} // namespace PkgInstallProgressWin32
