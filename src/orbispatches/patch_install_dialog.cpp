#include "orbispatches/patch_install_dialog.h"

#include "orbispatches/patch_download_panel.h"

namespace PatchInstall {

void Run(const OrbisPatches::PatchEntry& patch, const std::string& titleid,
         const std::string& game_name, HWND parent_hwnd, QWidget* parent_widget,
         bool download_only) {
    PatchDownload::Start(patch, titleid, game_name, parent_hwnd, parent_widget, download_only);
}

} // namespace PatchInstall
