cmake_minimum_required(VERSION 3.20)
project(libfatx_vendor LANGUAGES C)

# Vendored from XboxDev/fatx (libfatx), GPL-2.0-or-later.
# Upstream: https://github.com/XboxDev/fatx
# Upstream revision: c83ba2340bd2805d1832bf2071e51ebe6b99e723

add_library(libfatx_vendor STATIC
    fatx.c
    fatx_attr.c
    fatx_dev.c
    fatx_dir.c
    fatx_disk.c
    fatx_fat.c
    fatx_file.c
    fatx_log.c
    fatx_misc.c
    fatx_partition.c
    ext.c
)

target_include_directories(libfatx_vendor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(libfatx_vendor PUBLIC c_std_11)
target_compile_definitions(libfatx_vendor PUBLIC _FILE_OFFSET_BITS=64)
if(MSVC)
    set_property(TARGET libfatx_vendor PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_options(libfatx_vendor PRIVATE /W3)
endif()
