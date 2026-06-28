#include "install/pkg_merge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

#include "common/io_file.h"
#include "core/file_format/pkg.h"

namespace PkgMerge {

namespace {

bool HasPkgMagicStream(std::ifstream& in) {
    unsigned char magic[4] = {};
    in.read(reinterpret_cast<char*>(magic), sizeof(magic));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        return false;
    }
    return magic[0] == 0x7F && magic[1] == 'C' && magic[2] == 'N' && magic[3] == 'T';
}

int PieceOrderFromFilename(const std::filesystem::path& path) {
    const auto stem = path.stem().wstring();
    if (stem.size() <= 6 || _wcsnicmp(stem.c_str(), L"piece_", 6) != 0) {
        return -1;
    }
    try {
        return std::stoi(stem.substr(6));
    } catch (...) {
        return -1;
    }
}

std::string FormatSize(std::uintmax_t bytes) {
    std::ostringstream out;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        out << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024ULL * 1024ULL) {
        out << (bytes / (1024.0 * 1024.0)) << " MB";
    } else {
        out << bytes << " bytes";
    }
    return out.str();
}

} // namespace

bool HasPkgMagic(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    return HasPkgMagicStream(in);
}

void SortPiecePaths(std::vector<std::filesystem::path>& piece_files) {
    std::sort(piece_files.begin(), piece_files.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  const int order_a = PieceOrderFromFilename(a);
                  const int order_b = PieceOrderFromFilename(b);
                  if (order_a >= 0 && order_b >= 0 && order_a != order_b) {
                      return order_a < order_b;
                  }
                  if (order_a >= 0 && order_b < 0) {
                      return true;
                  }
                  if (order_a < 0 && order_b >= 0) {
                      return false;
                  }
                  return a.filename().wstring() < b.filename().wstring();
              });
}

const char* InvalidDownloadMessage(const std::filesystem::path& path, const std::size_t piece_count) {
    static thread_local std::string message;
    if (piece_count > 1) {
        message = "The merged patch file is not a valid PKG. One or more downloaded pieces may be "
                  "corrupt or incomplete.\n\nDelete the patch folder and download again from "
                  "ORBISPatches. This patch downloads as " +
                  std::to_string(piece_count) +
                  " piece files that are merged automatically before install.";
        return message.c_str();
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    message = "The downloaded patch file is not a valid PKG";
    if (!ec) {
        message += " (" + FormatSize(size) + ")";
        if (size == 4294967296ULL) {
            message += ". The file stopped at exactly 4 GB, which usually means the download was "
                       "incomplete";
        }
    }
    message += ".\n\nDelete the patch folder and download again from ORBISPatches.";
    return message.c_str();
}

bool IsStandaloneDeltaPkg(const std::filesystem::path& path) {
    if (!HasPkgMagic(path)) {
        return false;
    }

    Common::FS::IOFile file(path, Common::FS::FileAccessMode::Read);
    if (!file.IsOpen()) {
        return false;
    }

    PKGHeader header{};
    file.Read(header);
    return header.pfs_image_offset == u64{0} && header.pfs_image_size == u64{0};
}

const char* StandaloneDeltaPkgMessage() {
    return "This file is a Delta PKG, which only applies on a real PS4 when the previous patch "
           "version is already installed.\n\n"
           "To install updates in shadPS4, use the full patch from ORBISPatches:\n"
           "• Game → ORBISPatches → Download & Install\n"
           "• Or File → Install Packages (PKG) → ORBIS Update with all patch piece files "
           "(not the separate Delta PKG download on the ORBISPatches site).";
}

bool MergePkgPieces(const std::vector<std::filesystem::path>& pieces,
                    const std::filesystem::path& output, std::string& error) {
    if (pieces.empty()) {
        error = "No PKG pieces to merge.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Could not create merged PKG file.";
        return false;
    }

    std::array<char, 1024 * 1024> buffer{};
    for (const auto& piece : pieces) {
        std::ifstream in(piece, std::ios::binary);
        if (!in) {
            error = "Could not read patch piece: " + piece.filename().string();
            return false;
        }

        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize read = in.gcount();
            if (read <= 0) {
                break;
            }
            out.write(buffer.data(), read);
            if (!out) {
                error = "Failed while writing merged PKG file.";
                return false;
            }
        }
    }

    out.close();
    if (!HasPkgMagic(output)) {
        std::filesystem::remove(output, ec);
        error = InvalidDownloadMessage(pieces.front(), pieces.size());
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> PrepareInstallPaths(
    const std::vector<std::filesystem::path>& piece_files, const std::filesystem::path& work_dir,
    std::string& error) {
    if (piece_files.empty()) {
        error = "No PKG files found.";
        return {};
    }

    auto sorted = piece_files;
    SortPiecePaths(sorted);

    if (sorted.size() == 1) {
        if (!HasPkgMagic(sorted.front())) {
            error = InvalidDownloadMessage(sorted.front());
            return {};
        }
        if (IsStandaloneDeltaPkg(sorted.front())) {
            error = StandaloneDeltaPkgMessage();
            return {};
        }
        return sorted;
    }

    const auto merged = work_dir / L"merged.pkg";
    if (!MergePkgPieces(sorted, merged, error)) {
        return {};
    }
    if (IsStandaloneDeltaPkg(merged)) {
        std::error_code ec;
        std::filesystem::remove(merged, ec);
        error = StandaloneDeltaPkgMessage();
        return {};
    }
    return {merged};
}

} // namespace PkgMerge
