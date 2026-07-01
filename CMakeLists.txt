cmake_minimum_required(VERSION 3.24)

project(ShadPS4PkgPlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ShadPS4 PKG plugin requires x64 builds")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PickProxyDll)
include(FetchCryptoPP)
include(FindLauncherQt)

set(CMAKE_AUTOMOC ON)

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1
    GIT_SHALLOW TRUE
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

FetchContent_Declare(
    toml11
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG v4.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(toml11)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

set(PKG_SOURCES
    src/pkg/core/loader.cpp
    src/pkg/core/file_format/pkg.cpp
    src/pkg/core/file_format/pkg_type.cpp
    src/pkg/core/file_format/psf.cpp
    src/pkg/core/crypto/crypto.cpp
    src/pkg/common/io_file.cpp
    src/pkg/common/assert.cpp
    src/pkg/common/error.cpp
    src/pkg/common/path_util.cpp
    src/pkg/common/string_util.cpp
    src/pkg/common/logging/backend.cpp
)

set(PLUGIN_SOURCES
    src/proxy/dllmain.cpp
    src/plugin_module.cpp
    src/hook/window_finder.cpp
    src/hook/menu_hook.cpp
    src/hook/qt_menu_hook.cpp
    src/install/pkg_installer.cpp
    src/install/pkg_merge.cpp
    src/install/pkg_passcode.cpp
    src/install/pkg_install_progress_win32.cpp
    src/install/pkg_install_dialog.cpp
    src/install/pkg_router.cpp
    src/config/shad_config.cpp
)

add_library(ShadPS4PkgPlugin SHARED ${PKG_SOURCES} ${PLUGIN_SOURCES})

target_include_directories(ShadPS4PkgPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pkg"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${zlib_SOURCE_DIR}"
    "${zlib_BINARY_DIR}"
)

target_compile_definitions(ShadPS4PkgPlugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    NTDDI_VERSION=0x0A000006
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_link_libraries(ShadPS4PkgPlugin PRIVATE
    cryptopp::cryptopp
    zlibstatic
    toml11::toml11
    nlohmann_json::nlohmann_json
    Qt6::Widgets
    shlwapi
    ole32
    shell32
    comctl32
    user32
    gdi32
)

set_target_properties(ShadPS4PkgPlugin PROPERTIES
    OUTPUT_NAME "${SHADPS4_PROXY_DLL}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
)

if(MINGW)
    target_link_options(ShadPS4PkgPlugin PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS ShadPS4PkgPlugin RUNTIME DESTINATION .)

option(SHADPS4_BUILD_EXTRACT_TOOL "Build standalone PKG extraction test tool" OFF)
if(SHADPS4_BUILD_EXTRACT_TOOL)
    add_executable(extract_pkg tools/extract_pkg.cpp ${PKG_SOURCES})
    target_include_directories(extract_pkg PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/pkg"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${zlib_SOURCE_DIR}"
        "${zlib_BINARY_DIR}"
    )
    target_compile_definitions(extract_pkg PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        NTDDI_VERSION=0x0A000006
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
    target_link_libraries(extract_pkg PRIVATE
        cryptopp::cryptopp
        zlibstatic
    )
    set_target_properties(extract_pkg PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
    )
endif()
