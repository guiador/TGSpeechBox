set(ESPEAK_SRC "${ESPEAK_NG_DIR}/src/libespeak-ng")

# Generate config.h from espeak-ng's template
set(HAVE_MKSTEMP 0)
set(USE_ASYNC 0)
set(USE_KLATT 0)
set(USE_LIBPCAUDIO 0)
set(USE_LIBSONIC 0)
set(USE_MBROLA 0)
set(USE_SPEECHPLAYER 0)
set(PROJECT_VERSION "1.52.0.1")
set(ESPEAK_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${ESPEAK_SRC}/config.h.in" "${ESPEAK_CONFIG_DIR}/config.h")

# Phonemizer-only source files (22 from espeak-ng + 1 local stub)
add_library(espeak_phonemizer STATIC
    # Core phonemization
    "${ESPEAK_SRC}/speech.c"
    "${ESPEAK_SRC}/espeak_api.c"
    "${ESPEAK_SRC}/translate.c"
    "${ESPEAK_SRC}/translateword.c"
    "${ESPEAK_SRC}/dictionary.c"
    "${ESPEAK_SRC}/readclause.c"
    "${ESPEAK_SRC}/phonemelist.c"
    "${ESPEAK_SRC}/phoneme.c"
    "${ESPEAK_SRC}/numbers.c"
    "${ESPEAK_SRC}/encoding.c"
    "${ESPEAK_SRC}/ssml.c"
    "${ESPEAK_SRC}/compiledict.c"

    # Language setup
    "${ESPEAK_SRC}/tr_languages.c"
    "${ESPEAK_SRC}/langopts.c"
    "${ESPEAK_SRC}/voices.c"
    "${ESPEAK_SRC}/setlengths.c"

    # Data loading
    "${ESPEAK_SRC}/synthdata.c"
    "${ESPEAK_SRC}/soundicon.c"

    # Utility
    "${ESPEAK_SRC}/common.c"
    "${ESPEAK_SRC}/mnemonics.c"
    "${ESPEAK_SRC}/error.c"

    # Stubs for excluded synthesis code
    synth_stubs.c
)

target_include_directories(espeak_phonemizer
    BEFORE PRIVATE
        "${ESPEAK_CONFIG_DIR}"
    PUBLIC
        "${ESPEAK_NG_DIR}/src/include"
    PRIVATE
        "${ESPEAK_NG_DIR}/src/include/compat"
        "${ESPEAK_SRC}"
)

target_compile_definitions(espeak_phonemizer
    PRIVATE "PATH_ESPEAK_DATA=\"${CMAKE_INSTALL_PREFIX}/share/espeak-ng-data\""
    PUBLIC LIBESPEAK_NG_EXPORT=1
)

if(MSVC)
    target_compile_options(espeak_phonemizer PRIVATE /W3 /utf-8)
    target_compile_definitions(espeak_phonemizer PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(espeak_phonemizer PRIVATE
        -fPIC -fvisibility=hidden -fno-exceptions -fwrapv
        -Wno-unused-parameter -Wno-sign-compare
    )
endif()

target_link_libraries(espeak_phonemizer PRIVATE ucd)
if(NOT MSVC)
    target_link_libraries(espeak_phonemizer PRIVATE m)
endif()

set_property(TARGET espeak_phonemizer PROPERTY POSITION_INDEPENDENT_CODE ON)
