#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct ICoreWebView2;

namespace OrbisPatches {

struct PatchPiece;

using HttpDownloadProgressFn = std::function<void(int64_t received, int64_t total)>;
using HttpCancelFn = std::function<bool()>;
using PumpEventsFn = std::function<void()>;

bool ExportCookieHeader(ICoreWebView2* webview, const std::string& url, std::string& cookie_header,
                        const PumpEventsFn& pump_events, std::string& error);

bool DownloadUrlToFile(const std::string& url, const std::filesystem::path& destination,
                       const std::string& cookie_header, const std::string& referer,
                       const HttpDownloadProgressFn& progress, const HttpCancelFn& should_cancel,
                       std::string& error);

std::optional<uint64_t> ParsePieceSizeLabel(const std::string& size_label);

bool VerifyDownloadedPiece(const PatchPiece& piece, size_t piece_index, size_t piece_count,
                           const std::filesystem::path& path, std::string& error);

} // namespace OrbisPatches
