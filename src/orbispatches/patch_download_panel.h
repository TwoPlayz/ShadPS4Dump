#pragma once

#include <windows.h>

class QMainWindow;
class QWidget;

#include "orbispatches/orbispatches_client.h"

namespace PatchDownload {

void EnsureAttached(QMainWindow* main_window);

bool IsBusy();

bool ShouldBlockPlay(QMainWindow* main_window);

void Start(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
           const std::string& game_name, HWND parent_hwnd, QWidget* parent_widget,
           bool download_only = false);

} // namespace PatchDownload
