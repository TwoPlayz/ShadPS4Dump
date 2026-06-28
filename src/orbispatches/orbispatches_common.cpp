#include "orbispatches/orbispatches_common.h"

namespace OrbisPatches {

std::filesystem::path WebViewUserDataPath() {
    return std::filesystem::temp_directory_path() / L"ShadPS4PkgPlugin" / L"WebView2Fetcher";
}

} // namespace OrbisPatches
