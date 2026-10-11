cmake_minimum_required(VERSION 3.16)

# Static MSVC runtime for all targets (including jpegli/highway), so there's no VC++ redistributable dependency
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

project(naps2_jpeg C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Configure jpegli to build only its static library (we link it into our own shared library). jpegli itself is used
# unmodified as a submodule.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(opt TOOLS DEVTOOLS FUZZERS JPEGLI_LIBJPEG DOXYGEN MANPAGES BENCHMARK JNI SJPEG OPENEXR TCMALLOC)
    set(JPEGLI_ENABLE_${opt} OFF CACHE BOOL "" FORCE)
endforeach()
# jpegli's configure step requires a CMS (skcms or lcms2) even though the encoder library doesn't link one
set(JPEGLI_ENABLE_SKCMS ON CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(sources/jpegli EXCLUDE_FROM_ALL)
if(MSVC)
    # jpegli formats error messages as "<__FILE__>:<line>: <text>" into an 80-char buffer, so with full paths the
    # text gets truncated. jpegli's own CMake already trims the path for GCC/Clang; do the same for MSVC.
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sources/jpegli/" JPEGLI_SOURCE_PREFIX)
    target_compile_options(jpegli-static PRIVATE "/d1trimfile:${JPEGLI_SOURCE_PREFIX}")
endif()

add_library(naps2_jpeg SHARED src/naps2_jpeg.cc)
target_include_directories(naps2_jpeg PRIVATE sources/jpegli)
target_link_libraries(naps2_jpeg PRIVATE jpegli-static)
# Only export the naps2_jpeg_* API
set_target_properties(naps2_jpeg PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
    # The jpegli static library isn't built with hidden visibility, so restrict exports explicitly
    target_link_options(naps2_jpeg PRIVATE "LINKER:-exported_symbol,_naps2_jpeg_*")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Only export the naps2_jpeg_* API (not symbols from static libraries like the C++ runtime), and fail on
    # unresolved symbols
    set(NAPS2_JPEG_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/naps2_jpeg.version")
    target_link_options(naps2_jpeg PRIVATE "-Wl,--version-script=${NAPS2_JPEG_VERSION_SCRIPT}" -Wl,--no-undefined)
    set_target_properties(naps2_jpeg PROPERTIES LINK_DEPENDS "${NAPS2_JPEG_VERSION_SCRIPT}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Statically link the C++ runtime so we only depend on glibc (Zig always links its libc++ statically)
        target_link_options(naps2_jpeg PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
