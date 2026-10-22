# Builds the core (chiaki-lib, from the repo root) for the iPad mini 1 with
# ios9-armv7.cmake, LAN only, with mbedTLS and Opus from CK_IOS_PREFIX, and
# links all of it into one signed armv7 executable. Proves that the core
# configures, compiles and links against the iOS 9.3 SDK.
# Usage: ios/toolchain/core-check.sh
cmake_minimum_required(VERSION 3.24)
project(ck_ios_core_check C)

set(CK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# The iPad build: LAN only, mbedTLS and Opus, no desktop front ends or decoders.
set(CHIAKI_ENABLE_HOLEPUNCH OFF CACHE BOOL "" FORCE)
set(CHIAKI_LIB_ENABLE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CHIAKI_LIB_MBEDTLS_EXTERNAL_PROJECT OFF CACHE BOOL "" FORCE)
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_GUI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_CLI OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(CHIAKI_ENABLE_FFMPEG_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_PI_DECODER OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SETSU OFF CACHE STRING "" FORCE)
set(CHIAKI_ENABLE_SPEEX OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_JERASURE OFF CACHE STRING "" FORCE)
set(CHIAKI_USE_SYSTEM_NANOPB OFF CACHE STRING "" FORCE)
add_subdirectory("${CK_REPO_ROOT}" chiaki)

add_executable(ck-core-check main.c)
target_link_libraries(ck-core-check "$<LINK_LIBRARY:WHOLE_ARCHIVE,chiaki-lib>")
ck_ios_sign(ck-core-check)
