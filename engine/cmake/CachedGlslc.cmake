# Compiles one shader through a content-addressed cache. The key covers the
# compiler version, the flags, the shader and the shared shader library.
#   cmake -DGLSLC=... -DSOURCE=... -DOUTPUT=... -DFLAGS=a|b -DLIBRARY_DIR=... -DCACHE_DIR=... -P CachedGlslc.cmake
string(REPLACE "|" ";" flag_list "${FLAGS}")

execute_process(COMMAND "${GLSLC}" --version OUTPUT_VARIABLE compiler_version ERROR_QUIET)
file(READ "${SOURCE}" source_text)
get_filename_component(source_name "${SOURCE}" NAME)
set(key "${compiler_version}\n${FLAGS}\n${source_name}\n${source_text}")
file(GLOB library_files "${LIBRARY_DIR}/*.glsl")
list(SORT library_files)
foreach(library_file ${library_files})
    get_filename_component(library_name "${library_file}" NAME)
    file(READ "${library_file}" library_text)
    string(APPEND key "\n${library_name}\n${library_text}")
endforeach()
string(SHA256 hash "${key}")
set(cached "${CACHE_DIR}/${hash}.spv")

if(EXISTS "${cached}")
    file(COPY_FILE "${cached}" "${OUTPUT}")
    return()
endif()

execute_process(COMMAND "${GLSLC}" ${flag_list} "${SOURCE}" -o "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "glslc failed for ${SOURCE}")
endif()
file(MAKE_DIRECTORY "${CACHE_DIR}")
# Written under a temporary name first, so a parallel build never reads half a file.
string(RANDOM LENGTH 8 suffix)
file(COPY_FILE "${OUTPUT}" "${cached}.${suffix}.tmp")
file(RENAME "${cached}.${suffix}.tmp" "${cached}")
