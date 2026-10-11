# KRONOS_COMPILER_CACHE: put ccache or sccache in front of every C/C++ compile.
#   -DKRONOS_COMPILER_CACHE=auto     sccache if installed, else ccache
#   -DKRONOS_COMPILER_CACHE=ccache   or sccache: that one
# Empty (the default) builds without a cache.
set(KRONOS_COMPILER_CACHE "" CACHE STRING "Compiler cache: auto, ccache, sccache, or empty for none")

if(KRONOS_COMPILER_CACHE)
    if(KRONOS_COMPILER_CACHE STREQUAL "auto")
        find_program(KRONOS_COMPILER_CACHE_PROGRAM NAMES sccache ccache)
    else()
        find_program(KRONOS_COMPILER_CACHE_PROGRAM NAMES ${KRONOS_COMPILER_CACHE})
    endif()
    if(KRONOS_COMPILER_CACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER ${KRONOS_COMPILER_CACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${KRONOS_COMPILER_CACHE_PROGRAM})
        message(STATUS "Compiler cache: ${KRONOS_COMPILER_CACHE_PROGRAM}")
    else()
        message(WARNING "KRONOS_COMPILER_CACHE=${KRONOS_COMPILER_CACHE}: no such program found, building without a cache")
    endif()
endif()

# KRONOS_SHADER_CACHE_DIR: a folder of compiled shaders keyed by content (see
# CachedGlslc.cmake), so clean builds and CI reuse them. Empty: no cache.
set(KRONOS_SHADER_CACHE_DIR "" CACHE PATH "Folder for compiled shaders keyed by content; empty for none")
