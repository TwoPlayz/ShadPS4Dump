#pragma once

#include <windows.h>

class QWidget;

#include "orbispatches/orbispatches_client.h"

namespace PatchInstall {

void Run(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
         const std::string& game_name, HWND parent_hwnd, QWidget* parent_widget,
         bool download_only = false);

} // namespace PatchInstall
