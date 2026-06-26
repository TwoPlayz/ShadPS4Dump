include(FetchContent)

set(CRYPTOPP_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CRYPTOPP_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(CRYPTOPP_INSTALL OFF CACHE BOOL "" FORCE)
set(CRYPTOPP_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cryptopp-cmake
    GIT_REPOSITORY https://github.com/abdes/cryptopp-cmake.git
    GIT_TAG CRYPTOPP_8_9_0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cryptopp-cmake)
