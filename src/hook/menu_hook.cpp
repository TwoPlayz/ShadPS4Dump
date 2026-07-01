#include "hook/menu_hook.h"

#include <commctrl.h>
#include <shellapi.h>
#include <string>

#include "hook/qt_menu_hook.h"
#include "hook/window_finder.h"
#include "install/pkg_installer.h"

namespace MenuHook {

namespace {

constexpr UINT kInstallGamePkgCommandId = 0x8F00;
constexpr UINT_PTR kSubclassId = 1;

HWND g_main_hwnd = nullptr;
bool g_menu_installed = false;
WNDPROC g_original_wndproc = nullptr;

static std::wstring StripMnemonic(std::wstring text) {
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&' && i + 1 < text.size()) {
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

static bool MenuTextEquals(HMENU menu, UINT index, UINT flags, const wchar_t* expected) {
    const UINT len = GetMenuStringW(menu, index, nullptr, 0, flags);
    if (len == 0) {
        return false;
    }
    std::wstring buffer(len, L'\0');
    GetMenuStringW(menu, index, buffer.data(), len + 1, flags);
    return _wcsicmp(StripMnemonic(buffer).c_str(), expected) == 0;
}

static HMENU FindSubmenuByText(HMENU parent, const wchar_t* text) {
    const int count = GetMenuItemCount(parent);
    for (int i = 0; i < count; ++i) {
        if (MenuTextEquals(parent, i, MF_BYPOSITION, text)) {
            return GetSubMenu(parent, i);
        }
    }
    return nullptr;
}

static int FindMenuItemByText(HMENU menu, const wchar_t* text) {
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        if (MenuTextEquals(menu, i, MF_BYPOSITION, text)) {
            return i;
        }
    }
    return -1;
}

static bool InstallPkgMenuItem(HWND hwnd) {
    HMENU menu_bar = GetMenu(hwnd);
    if (!menu_bar) {
        return false;
    }

    HMENU file_menu = FindSubmenuByText(menu_bar, L"File");
    if (!file_menu) {
        file_menu = GetSubMenu(menu_bar, 0);
    }
    if (!file_menu) {
        return false;
    }

    for (int i = 0; i < GetMenuItemCount(file_menu); ++i) {
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_ID;
        if (GetMenuItemInfoW(file_menu, i, TRUE, &mii) && mii.wID == kInstallGamePkgCommandId) {
            return true;
        }
    }

    int insert_before = FindMenuItemByText(file_menu, L"Exit");
    if (insert_before < 0) {
        insert_before = GetMenuItemCount(file_menu);
    }

    MENUITEMINFOW separator{};
    separator.cbSize = sizeof(separator);
    separator.fMask = MIIM_TYPE | MIIM_STATE;
    separator.fType = MFT_SEPARATOR;
    separator.fState = MFS_ENABLED;
    InsertMenuItemW(file_menu, insert_before, TRUE, &separator);

    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
    item.fType = MFT_STRING;
    item.fState = MFS_ENABLED;
    item.wID = kInstallGamePkgCommandId;
    item.dwTypeData = const_cast<LPWSTR>(L"Install Game PKG...");
    InsertMenuItemW(file_menu, insert_before + 1, TRUE, &item);

    DrawMenuBar(hwnd);
    return true;
}

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wparam) == kInstallGamePkgCommandId) {
            PkgInstaller::RunInstallGameDialog(hwnd);
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        PkgInstaller::HandleDroppedFiles(hwnd, drop);
        DragFinish(drop);
        return 0;
    }
    default:
        break;
    }

    return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

static void EnableDragDrop(HWND hwnd) {
    DragAcceptFiles(hwnd, TRUE);
    ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ADD, nullptr);
}

static bool InstallHooks(HWND hwnd) {
    bool menu_installed = InstallPkgMenuItem(hwnd);
    if (!menu_installed) {
        menu_installed = QtMenuHook::TryInstallMenuItem();
    }
    if (!menu_installed) {
        return false;
    }

    if (!g_original_wndproc) {
        g_original_wndproc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        SetWindowSubclass(hwnd, SubclassProc, kSubclassId, 0);
        EnableDragDrop(hwnd);
    }

    g_main_hwnd = hwnd;
    g_menu_installed = true;
    return true;
}

static DWORD WINAPI HookThread(LPVOID) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        HWND hwnd = WindowFinder::FindLauncherWindow();
        if (hwnd) {
            if (InstallHooks(hwnd)) {
                return 0;
            }
        }
        Sleep(200);
    }
    return 1;
}

} // namespace

void StartMenuHookThread() {
    HANDLE thread = CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

void TriggerRefreshGameList(HWND hwnd) {
    if (!hwnd) {
        hwnd = g_main_hwnd;
    }

    if (hwnd) {
        HMENU menu_bar = GetMenu(hwnd);
        if (menu_bar) {
            HMENU game_menu = FindSubmenuByText(menu_bar, L"Game");
            if (game_menu) {
                const int index = FindMenuItemByText(game_menu, L"Refresh Game List");
                if (index >= 0) {
                    MENUITEMINFOW mii{};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_ID;
                    if (GetMenuItemInfoW(game_menu, index, TRUE, &mii)) {
                        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(mii.wID, 0), 0);
                    }
                }
            }
        }
    }

    QtMenuHook::TriggerRefreshGameList();
}

} // namespace MenuHook
