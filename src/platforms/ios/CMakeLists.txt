# Static libraries for iOS/macOS Apple platform builds
#
# Usage (macOS desktop):
#   cmake -B build/desktop -DCMAKE_BUILD_TYPE=MinSizeRel
#   cmake --build build/desktop
#
# Usage (iOS):
#   cmake -B build/ios -DCMAKE_BUILD_TYPE=MinSizeRel \
#         -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
#         -DCMAKE_OSX_SYSROOT=iphoneos
#   cmake --build build/ios

cmake_minimum_required(VERSION 3.21)
project(tgsb_apple C CXX)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(ESPEAK_NG_DIR "${REPO_ROOT}/../espeak-ng" CACHE PATH "Path to espeak-ng source")
set(TGSB_DIR "${REPO_ROOT}" CACHE PATH "Path to TGSpeechBox source")

# ucd-tools (vendored in espeak-ng)
add_subdirectory(
    "${ESPEAK_NG_DIR}/src/ucd-tools"
    "${CMAKE_CURRENT_BINARY_DIR}/ucd-tools"
)

add_subdirectory("${TGSB_DIR}/espeak-phonemizer" "${CMAKE_CURRENT_BINARY_DIR}/espeak_phonemizer")
add_subdirectory("${TGSB_DIR}/tgsb-static" "${CMAKE_CURRENT_BINARY_DIR}/tgsb_static")
