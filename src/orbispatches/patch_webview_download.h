#pragma once

#include "orbispatches/orbispatches_client.h"

#include <filesystem>
#include <functional>
#include <string>
#include <windows.h>

namespace OrbisPatches {

using PumpEventsFn = std::function<void()>;

bool DownloadUrlViaWebView(const std::string& url, const std::filesystem::path& destination,
                           const DownloadProgressFn& progress, std::string& error,
                           CancelCallback should_cancel = {}, HWND parent_hwnd = nullptr,
                           const PumpEventsFn& pump_events = {});

} // namespace OrbisPatches
