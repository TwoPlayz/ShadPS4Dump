#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace OrbisPatches {

struct PatchPiece {
    int order = 0;
    std::string size_label;
    std::string hash;
    std::string pkg_url;
};

class PatchLinkFetcher {
public:
    using PumpEventsFn = std::function<void()>;

    static bool FetchPieces(const std::string& titleid, const std::string& contentver,
                            const std::string& patch_key, std::vector<PatchPiece>& pieces,
                            std::string& error, HWND parent_hwnd = nullptr,
                            const PumpEventsFn& pump_events = {});
};

} // namespace OrbisPatches
