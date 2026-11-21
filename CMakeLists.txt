cmake_minimum_required(VERSION 3.18)

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
    # text gets truncated. jpegli's own CMake already trims the path for GCC/Clang; do the same for MSVC and clang-cl
    # (which needs the Clang option passed through).
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sources/jpegli/" JPEGLI_SOURCE_PREFIX)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(jpegli-static PRIVATE "/d1trimfile:${JPEGLI_SOURCE_PREFIX}")
    else()
        target_compile_options(jpegli-static PRIVATE "/clang:-fmacro-prefix-map=${JPEGLI_SOURCE_PREFIX}=")
    endif()
endif()

# Builds only the JPEG encoder (without libjxl/libwebp/skcms)
option(NAPS2_JPEG_ONLY "Build only the JPEG encoder" OFF)

if(NAPS2_JPEG_ONLY)
    add_library(naps2_jpeg SHARED src/naps2_jpeg.cc)
    target_include_directories(naps2_jpeg PRIVATE sources/jpegli)
    target_link_libraries(naps2_jpeg PRIVATE jpegli-static)
else()
    # Configure libjxl to build only its static libraries. It shares jpegli's copy of highway (two copies can't be
    # in the same build as they'd define the same targets), which it finds as if it were a system library.
    set(JPEGXL_FORCE_SYSTEM_HWY ON CACHE BOOL "" FORCE)
    set(HWY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sources/jpegli/third_party/highway" CACHE PATH "" FORCE)
    set(HWY_LIBRARY hwy CACHE STRING "" FORCE)
    foreach(opt TOOLS DEVTOOLS FUZZERS DOXYGEN MANPAGES BENCHMARK EXAMPLES JNI SJPEG OPENEXR TCMALLOC VIEWERS PLUGINS
            JPEGLI)
        set(JPEGXL_ENABLE_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(JPEGXL_ENABLE_SKCMS ON CACHE BOOL "" FORCE)
    set(JPEGXL_BUNDLE_LIBPNG OFF CACHE BOOL "" FORCE)
    # Avoids a git version target in libjxl's tools that conflicts with jpegli's
    set(JPEGXL_VERSION "0.13.0")
    # libjxl's own setup of skcms's AVX2/AVX-512 variants doesn't work for our builds: it only checks
    # CMAKE_SYSTEM_PROCESSOR (which is AMD64 on Windows), and its "-march=x86-64" flag isn't accepted by Zig. So
    # disable it (these were already set by jpegli's compiler checks, so they have to be overridden) and add the
    # variants below instead.
    set(CXX_MAVX2_SUPPORTED OFF CACHE INTERNAL "")
    set(CXX_MAVX512F_SUPPORTED OFF CACHE INTERNAL "")
    add_subdirectory(sources/libjxl EXCLUDE_FROM_ALL)

    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #if !defined(__x86_64__) || !(defined(__clang__) || defined(__GNUC__))
        #error
        #endif
        int main() { return 0; }" NAPS2_SKCMS_X64_SIMD)
    if(NAPS2_SKCMS_X64_SIMD)
        set(SKCMS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sources/libjxl/third_party/skcms/src")
        target_sources(jxl_cms PRIVATE "${SKCMS_DIR}/skcms_TransformHsw.cc" "${SKCMS_DIR}/skcms_TransformSkx.cc")
        set_source_files_properties("${SKCMS_DIR}/skcms_TransformHsw.cc" TARGET_DIRECTORY jxl_cms
            PROPERTIES COMPILE_OPTIONS "-mavx2;-mf16c")
        set_source_files_properties("${SKCMS_DIR}/skcms_TransformSkx.cc" TARGET_DIRECTORY jxl_cms
            PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512dq;-mavx512cd;-mavx512bw;-mavx512vl")
        get_target_property(JXL_CMS_DEFINITIONS jxl_cms COMPILE_DEFINITIONS)
        list(REMOVE_ITEM JXL_CMS_DEFINITIONS SKCMS_DISABLE_HSW SKCMS_DISABLE_SKX)
        set_target_properties(jxl_cms PROPERTIES COMPILE_DEFINITIONS "${JXL_CMS_DEFINITIONS}")
    endif()

    # Configure libwebp to build only its libraries
    foreach(opt ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS)
        set(WEBP_BUILD_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
    add_subdirectory(sources/libwebp EXCLUDE_FROM_ALL)
    if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        file(GLOB WEBP_NEON_FILES sources/libwebp/src/dsp/*_neon.c sources/libwebp/sharpyuv/*_neon.c)
        set_source_files_properties(${WEBP_NEON_FILES} DIRECTORY sources/libwebp PROPERTIES COMPILE_FLAGS "")
    endif()

    add_library(naps2_jpeg SHARED src/naps2_jpeg.cc src/naps2_webp.cc src/naps2_jxl.cc src/naps2_util.cc
        src/naps2_icc.cc)
    # skcms (built into libjxl's jxl_cms) is also used to apply color profiles in WebP images
    target_include_directories(naps2_jpeg PRIVATE sources/jpegli sources/libwebp/src sources/libjxl/third_party/skcms)
    target_link_libraries(naps2_jpeg PRIVATE jpegli-static jxl jxl_threads webp webpdemux libwebpmux)
endif()
# Only export the naps2_* API
set_target_properties(naps2_jpeg PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
    # The static libraries aren't all built with hidden visibility, so restrict exports explicitly
    target_link_options(naps2_jpeg PRIVATE "LINKER:-exported_symbol,_naps2_*")
    # Remove unused code and local symbols
    target_link_options(naps2_jpeg PRIVATE "LINKER:-dead_strip" "LINKER:-x")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Only export the naps2_* API (not symbols from static libraries like the C++ runtime), and fail on
    # unresolved symbols
    set(NAPS2_JPEG_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/src/naps2_jpeg.version")
    target_link_options(naps2_jpeg PRIVATE "-Wl,--version-script=${NAPS2_JPEG_VERSION_SCRIPT}" -Wl,--no-undefined)
    set_target_properties(naps2_jpeg PROPERTIES LINK_DEPENDS "${NAPS2_JPEG_VERSION_SCRIPT}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Statically link the C++ runtime so we only depend on glibc (Zig always links its libc++ statically)
        target_link_options(naps2_jpeg PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
