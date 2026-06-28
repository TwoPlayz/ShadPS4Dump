#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core/file_format/pkg.h"

namespace {

void Log(const std::string& message) {
    std::ofstream log("extract_pkg.log", std::ios::app);
    log << message << '\n';
    std::cout << message << '\n';
    std::cout.flush();
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::remove("extract_pkg.log");
    if (argc < 3) {
        std::cerr << "Usage: extract_pkg <input.pkg> <output_dir>\n";
        return 1;
    }

    const std::filesystem::path pkg_file = argv[1];
    const std::filesystem::path extract_path = argv[2];

    PKG pkg;
    std::string failreason;
    Log("Opening PKG...");
    if (!pkg.Open(pkg_file, failreason)) {
        Log(std::string("Open failed: ") + failreason);
        return 1;
    }

    Log(std::string("Title ID: ") + std::string(pkg.GetTitleID()));
    Log(std::string("Flags: ") + pkg.GetPkgFlags());
    const auto header = pkg.GetPkgHeader();
    Log("pfs_image_offset=" + std::to_string(static_cast<unsigned long long>(header.pfs_image_offset)));
    Log("pfs_image_size=" + std::to_string(static_cast<unsigned long long>(header.pfs_image_size)));
    Log("pfs_cache_size=" + std::to_string(static_cast<unsigned>(header.pfs_cache_size)));
    Log("pkg_size=" + std::to_string(static_cast<unsigned long long>(header.pkg_size)));

    std::error_code ec;
    std::filesystem::create_directories(extract_path, ec);

    Log("Running Extract()...");
    if (!pkg.Extract(pkg_file, extract_path, failreason)) {
        Log(std::string("Extract failed: ") + failreason);
        return 1;
    }

    const int file_count = pkg.GetNumberOfFiles();
    Log("Extracting " + std::to_string(file_count) + " file(s)...");
    for (int i = 0; i < file_count; ++i) {
        Log("File index " + std::to_string(i));
        pkg.ExtractFiles(i);
    }

    const auto eboot = extract_path / "eboot.bin";
    if (!std::filesystem::exists(eboot)) {
        Log("Missing eboot.bin after extraction");
        return 1;
    }

    Log("Done. eboot.bin size: " + std::to_string(std::filesystem::file_size(eboot)));
    return 0;
}
