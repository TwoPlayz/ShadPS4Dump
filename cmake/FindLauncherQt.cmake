# Locate Qt 6 Widgets for menu injection.
# Priority: CMAKE_PREFIX_PATH / Qt6_DIR, then SHADPS4_LAUNCHER_DIR (aqt/vcpkg hint), then vcpkg.

if(DEFINED SHADPS4_LAUNCHER_DIR AND SHADPS4_LAUNCHER_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${SHADPS4_LAUNCHER_DIR}")
endif()

find_package(Qt6 6.5 COMPONENTS Widgets QUIET)

if(NOT Qt6_FOUND AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE STRING "" FORCE)
    find_package(Qt6 6.5 COMPONENTS Widgets QUIET)
endif()

if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt6 Widgets is required to inject the File menu (Qt draws menus, not Win32 HMENU).\n"
        "Install Qt 6.10+ for MSVC x64, then reconfigure with:\n"
        "  cmake -B build -G \"Visual Studio 17 2022\" -A x64 "
        "-DCMAKE_PREFIX_PATH=\"C:/Qt/6.10.0/msvc2022_64\" "
        "-DSHADPS4_LAUNCHER_DIR=\"C:/path/to/shadPS4QtLauncher/folder\"\n"
        "Quick install: pip install aqtinstall && "
        "aqt install-qt windows desktop 6.10.0 win64_msvc2022_64 -O .qt")
endif()

message(STATUS "Using Qt6 Widgets ${Qt6_VERSION} for menu hook")
