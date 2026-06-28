#pragma once

namespace QtMenuHook {

// Inject "Install Packages (PKG)" into the Qt File menu (main thread).
bool TryInstallMenuItem();

// Trigger Game -> Refresh Game List on the Qt launcher (any thread).
void TriggerRefreshGameList();

} // namespace QtMenuHook
