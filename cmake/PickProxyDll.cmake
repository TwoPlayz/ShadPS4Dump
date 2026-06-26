# ShadPS4 PKG Plugin — proxy DLL selection helper
#
# shadPS4QtLauncher.exe imports VERSION.dll and WINMM.dll (verified on v224).
# This project ships version.dll (17 exports) as the default proxy.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SHADPS4_PROXY_DLL)
    set(SHADPS4_PROXY_DLL "version" CACHE STRING "Proxy DLL base name (without .dll)")
endif()

message(STATUS "ShadPS4 PKG plugin proxy target: ${SHADPS4_PROXY_DLL}.dll")

# Optional: verify imports when SHADPS4_QTLAUNCHER_EXE is set.
if(SHADPS4_QTLAUNCHER_EXE AND EXISTS "${SHADPS4_QTLAUNCHER_EXE}")
    find_program(DUMPBIN_EXECUTABLE dumpbin
        HINTS
            "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
    )
    if(DUMPBIN_EXECUTABLE)
        execute_process(
            COMMAND "${DUMPBIN_EXECUTABLE}" /imports "${SHADPS4_QTLAUNCHER_EXE}"
            OUTPUT_VARIABLE _imports
            ERROR_QUIET
        )
        string(TOUPPER "${SHADPS4_PROXY_DLL}.dll" _proxy_upper)
        if(NOT _imports MATCHES "${_proxy_upper}")
            message(WARNING
                "${SHADPS4_QTLAUNCHER_EXE} may not import ${SHADPS4_PROXY_DLL}.dll. "
                "Set -DSHADPS4_PROXY_DLL=winmm if needed.")
        else()
            message(STATUS "Verified ${SHADPS4_PROXY_DLL}.dll is imported by QtLauncher")
        endif()
    endif()
endif()
