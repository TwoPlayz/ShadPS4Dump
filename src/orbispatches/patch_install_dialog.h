#pragma once

#include <windows.h>

class QWidget;

#include "orbispatches/orbispatches_client.h"

namespace PatchInstall {

void Run(const OrbisPatches::PatchEntry& patch, const std::string& titleid, HWND parent_hwnd,
         QWidget* parent_widget);

} // namespace PatchInstall
