#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace OrbisPatches {

struct SearchResult {
    std::string titleid;
    std::string name;
    std::string region;
    std::string icon_url;
};

struct PatchEntry {
    std::string version;
    std::string filesize;
    std::string required_firmware;
    std::string creation_date;
    std::string changelog_preview;
    bool is_latest = false;
    std::string patch_key;
};

struct LoadParams {
    std::string titleid;
    std::string key;
};

using DownloadProgressFn = std::function<void(int64_t bytes_received, int64_t total_bytes)>;

class Client {
public:
    static std::vector<SearchResult> Search(const std::string& term, std::string& error);
    static std::optional<LoadParams> FetchLoadParams(const std::string& titleid, std::string& error);
    static std::vector<PatchEntry> LoadPatches(const std::string& titleid, const std::string& key,
                                               std::string& error);
    static bool DownloadUrl(const std::string& url, const std::filesystem::path& destination,
                            const DownloadProgressFn& progress, std::string& error);
};

} // namespace OrbisPatches
