cmake_minimum_required(VERSION 3.21)

project(speechPlayer)

# Threads are needed on POSIX (LockableObject uses std::recursive_mutex).
# On Windows this is not required.
if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

# ---------------------------------------
# Global MSVC settings (lean + UTF-8)
# ---------------------------------------
if(MSVC)
  # Make the compiler treat source files as UTF-8 (important for IPA symbols).
  add_compile_options(/utf-8)

  # Prefer dynamic MSVC runtime (smaller DLLs than /MT in most cases).
  # If you rely on /MT for deployment reasons, remove this line.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

  # Release optimizations and dead code stripping.
  add_compile_options("$<$<CONFIG:Release>:/O2>" "$<$<CONFIG:Release>:/DNDEBUG>")
  add_compile_options("$<$<CONFIG:Release>:/Gy>" "$<$<CONFIG:Release>:/Gw>")
  add_compile_options("$<$<CONFIG:Release>:/GL>")

  add_link_options("$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
  add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()

# -------------------------
# speechPlayer.dll (DSP)
# -------------------------
file(GLOB SRC_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(SPEECHPLAYER_SOURCES ${SRC_CPP})
if(WIN32)
  # Export list for the legacy DLL ABI.
  list(APPEND SPEECHPLAYER_SOURCES "src/speechPlayer.def")
endif()

add_library(speechPlayer SHARED ${SPEECHPLAYER_SOURCES})

target_include_directories(speechPlayer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(speechPlayer PRIVATE cxx_std_17)

# Windows waveOut dependency (as in the original project).
if(WIN32)
  target_link_libraries(speechPlayer PRIVATE winmm)
else()
  target_link_libraries(speechPlayer PRIVATE Threads::Threads)
endif()

# -------------------------
# nvspFrontend.dll (IPA -> Frames)
# -------------------------
file(GLOB FRONTEND_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/*.cpp")
file(GLOB FRONTEND_PASSES_CPP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/passes/*.cpp")

add_library(nvspFrontend SHARED ${FRONTEND_CPP} ${FRONTEND_PASSES_CPP})

target_include_directories(nvspFrontend PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend"
)

target_compile_features(nvspFrontend PRIVATE cxx_std_17)

target_compile_definitions(nvspFrontend PRIVATE NVSP_FRONTEND_EXPORTS=1)

# Optional: make the DLL name predictable.
set_target_properties(nvspFrontend PROPERTIES OUTPUT_NAME "nvspFrontend")

# -------------------------
# Optional command-line tools
# -------------------------
option(NVSP_BUILD_TOOLS "Build optional command-line tools (e.g. Speech Dispatcher helpers)" OFF)
if(NVSP_BUILD_TOOLS)
  add_executable(nvspRender tools/nvspRender.cpp)
  target_include_directories(nvspRender PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend"
  )
  target_compile_features(nvspRender PRIVATE cxx_std_17)
  target_link_libraries(nvspRender PRIVATE speechPlayer nvspFrontend)
endif()

# -------------------------
# Win32 phoneme editor GUI
# -------------------------
if(WIN32)
  add_subdirectory(tools/tgsbPhonemeEditorWin32)
endif()

# -------------------------
# SAPI5 engine + settings app (opt-in, requires eSpeak-NG source)
# -------------------------
option(TGSB_BUILD_SAPI "Build the SAPI5 TTS engine and settings app (requires -DESPEAK_NG_DIR=...)" OFF)

if(WIN32 AND TGSB_BUILD_SAPI)
  # eSpeak-NG source (never committed; local checkout required).
  set(ESPEAK_NG_DIR "" CACHE PATH "Path to eSpeak-NG source checkout")
  if(ESPEAK_NG_DIR STREQUAL "")
    message(FATAL_ERROR "TGSB_BUILD_SAPI is ON but ESPEAK_NG_DIR is not set. Pass -DESPEAK_NG_DIR=<path-to-espeak-ng-source>")
  endif()
  set(TGSB_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Path to TGSpeechBox source root")

  # --- Static libraries ---

  # ucd-tools (Unicode Character Database, vendored in eSpeak-NG)
  add_subdirectory(
    "${ESPEAK_NG_DIR}/src/ucd-tools"
    "${CMAKE_CURRENT_BINARY_DIR}/ucd-tools"
  )

  # espeak_phonemizer.lib (text → IPA, GPL)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/espeak-phonemizer"
    "${CMAKE_CURRENT_BINARY_DIR}/espeak_phonemizer"
  )

  # tgspeechbox_static.lib (DSP) + tgsbFrontend_static.lib (frontend)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/tgsb-static"
    "${CMAKE_CURRENT_BINARY_DIR}/tgsb_static"
  )

  # --- TGSpeechSapi.dll (statically linked) ---

  add_library(TGSpeechSapi SHARED
    src/sapi/TGSpeechSapi.def
    src/sapi/sapi_main.cpp
    src/sapi/com.cpp
    src/sapi/registry.cpp
    src/sapi/ISpDataKeyImpl.cpp
    src/sapi/voice_token.cpp
    src/sapi/tgsb_settings.cpp
    src/sapi/IEnumSpObjectTokensImpl.cpp
    src/sapi/ISpTTSEngineImpl.cpp
    src/sapi/tgsb_runtime.cpp
  )

  target_include_directories(TGSpeechSapi PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sapi"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend"
  )

  target_compile_definitions(TGSpeechSapi PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  )

  target_compile_features(TGSpeechSapi PRIVATE cxx_std_17)

  target_link_libraries(TGSpeechSapi PRIVATE
    tgspeechbox_static      # DSP engine
    tgsbFrontend_static     # IPA → frames
    espeak_phonemizer       # text → IPA
    ole32 oleaut32 advapi32 shlwapi uuid
  )

  if(MSVC)
    target_compile_options(TGSpeechSapi PRIVATE /W4 /permissive-)
  endif()

  set_target_properties(TGSpeechSapi PROPERTIES OUTPUT_NAME "TGSpeechSapi")

  # --- TGSpeechSapiSettings.exe ---

  add_executable(TGSpeechSapiSettings WIN32
    tools/tgsbSapiSettings/settings_app.cpp
    tools/tgsbSapiSettings/settings_app.rc
  )

  target_compile_features(TGSpeechSapiSettings PRIVATE cxx_std_17)

  target_compile_definitions(TGSpeechSapiSettings PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  )

  target_link_libraries(TGSpeechSapiSettings PRIVATE comctl32)

  if(MSVC)
    target_compile_options(TGSpeechSapiSettings PRIVATE /W4 /permissive-)
  endif()
endif()
