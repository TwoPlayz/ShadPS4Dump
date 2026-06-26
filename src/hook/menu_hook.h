#pragma once

#include <windows.h>

namespace MenuHook {

// Poll until the launcher window exists, inject menu item, and install subclass.
void StartMenuHookThread();

// Trigger "Refresh Game List" on the launcher main window.
void TriggerRefreshGameList(HWND hwnd);

} // namespace MenuHook
