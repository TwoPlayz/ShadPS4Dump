set(WEBVIEW2_VERSION "1.0.2592.51" CACHE STRING "Microsoft.Web.WebView2 NuGet version")
set(WEBVIEW2_ROOT "${CMAKE_BINARY_DIR}/_deps/webview2" CACHE PATH "Extracted WebView2 SDK root")

set(_WEBVIEW2_MARKER "${WEBVIEW2_ROOT}/build/native/include/WebView2.h")
if(NOT EXISTS "${_WEBVIEW2_MARKER}")
    message(STATUS "Fetching Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}")
    set(_WEBVIEW2_NUPKG "${CMAKE_BINARY_DIR}/webview2.nupkg")
    file(DOWNLOAD
        "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
        "${_WEBVIEW2_NUPKG}"
        SHOW_PROGRESS
        TLS_VERIFY ON
    )
    file(MAKE_DIRECTORY "${WEBVIEW2_ROOT}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xf "${_WEBVIEW2_NUPKG}"
        WORKING_DIRECTORY "${WEBVIEW2_ROOT}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

add_library(WebView2Loader STATIC IMPORTED GLOBAL)
set_target_properties(WebView2Loader PROPERTIES
    IMPORTED_LOCATION "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${WEBVIEW2_ROOT}/build/native/include"
)
