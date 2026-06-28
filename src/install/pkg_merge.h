#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace PkgMerge {

bool HasPkgMagic(const std::filesystem::path& path);

// Sony delta packages (-DP.pkg) have no PFS image; they cannot be installed here.
bool IsStandaloneDeltaPkg(const std::filesystem::path& path);

const char* StandaloneDeltaPkgMessage();

// Concatenate split OPKG piece PKGs into one installable PKG.
bool MergePkgPieces(const std::vector<std::filesystem::path>& pieces,
                    const std::filesystem::path& output, std::string& error);

// Validates piece PKG(s) and merges when needed. Returns install-ready paths.
std::vector<std::filesystem::path> PrepareInstallPaths(
    const std::vector<std::filesystem::path>& piece_files, const std::filesystem::path& work_dir,
    std::string& error);

void SortPiecePaths(std::vector<std::filesystem::path>& piece_files);

const char* InvalidDownloadMessage(const std::filesystem::path& path, std::size_t piece_count = 1);

} // namespace PkgMerge
