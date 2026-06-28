#pragma once

#include <cstdint>
#include <filesystem>
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
    using CancelCallback = std::function<bool()>;
    using PieceProgressFn =
        std::function<void(size_t piece_index, size_t piece_count, int64_t received, int64_t total)>;

    static bool FetchPieces(const std::string& titleid, const std::string& contentver,
                            const std::string& patch_key, std::vector<PatchPiece>& pieces,
                            std::string& error, HWND parent_hwnd = nullptr,
                            const PumpEventsFn& pump_events = {});

    // Fetch download links and download all pieces in one WebView session.
    static bool FetchAndDownloadPieces(const std::string& titleid, const std::string& contentver,
                                       const std::string& patch_key,
                                       const std::filesystem::path& download_dir,
                                       std::vector<std::filesystem::path>& downloaded_paths,
                                       const PieceProgressFn& progress, std::string& error,
                                       HWND parent_hwnd = nullptr,
                                       const PumpEventsFn& pump_events = {},
                                       CancelCallback should_cancel = {});
};

} // namespace OrbisPatches
