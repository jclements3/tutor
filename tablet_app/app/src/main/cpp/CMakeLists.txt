cmake_minimum_required(VERSION 3.22.1)
project(musictutor_synth)

# ─── fluidlite (stripped-down FluidSynth) ───────────────────────────────────
# Vendored under fluidlite/. SF3 / Ogg-Vorbis support disabled so we don't
# need libogg/libvorbis — TimGM6mb.sf2 ships as uncompressed SF2 anyway.
file(GLOB FLUIDLITE_SRC fluidlite/src/*.c)
# Exclude:
#   - fluidsynth.c   : the CLI executable's main(), not part of the lib
#   - fluid_dsp_simple.c : alternative DSP that's #include'd into a function
#                          body, not compilable standalone (WITH_FLOAT
#                          builds use fluid_dsp_float.c instead)
list(FILTER FLUIDLITE_SRC EXCLUDE REGEX "/fluidsynth\\.c$")
list(FILTER FLUIDLITE_SRC EXCLUDE REGEX "/fluid_dsp_simple\\.c$")
add_library(fluidlite STATIC ${FLUIDLITE_SRC})
target_include_directories(fluidlite PUBLIC
    fluidlite/include
    fluidlite/src
    fluidlite/src/watcom)     # pre-built fluid_config.h + version.h
target_compile_definitions(fluidlite PRIVATE
    HAVE_STDLIB_H HAVE_STDIO_H HAVE_STDARG_H HAVE_STRING_H
    HAVE_FLOAT_H HAVE_LIMITS_H HAVE_MATH_H
    WITH_FLOAT=1)
target_compile_options(fluidlite PRIVATE -w)   # silence fluidlite warnings

# ─── Oboe (provided by the Gradle prefab dependency) ───────────────────────
find_package(oboe REQUIRED CONFIG)

# ─── Our native synth library ───────────────────────────────────────────────
add_library(musictutor_synth SHARED oboe_synth.cpp)

target_link_libraries(musictutor_synth
    PRIVATE
        fluidlite
        oboe::oboe
        android
        log
        m)
