#include "hook/window_finder.h"

#include <array>
#include <cstring>

namespace WindowFinder {

struct EnumData {
    HWND result = nullptr;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
    auto* data = reinterpret_cast<EnumData*>(lparam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t title[512] = {};
    const int len = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (len <= 0) {
        return TRUE;
    }

    if (wcsncmp(title, L"shadPS4QtLauncher", 17) == 0) {
        data->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindLauncherWindow() {
    EnumData data;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.result;
}

} // namespace WindowFinder
