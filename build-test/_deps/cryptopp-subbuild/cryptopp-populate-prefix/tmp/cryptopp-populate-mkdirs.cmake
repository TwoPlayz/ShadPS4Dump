# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-cmake-build/cryptopp")
  file(MAKE_DIRECTORY "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-cmake-build/cryptopp")
endif()
file(MAKE_DIRECTORY
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-build"
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix"
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/tmp"
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/src/cryptopp-populate-stamp"
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/src"
  "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/src/cryptopp-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/src/cryptopp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/JustinPC/Documents/GitHub/ShadPS4Dump/build-test/_deps/cryptopp-subbuild/cryptopp-populate-prefix/src/cryptopp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
