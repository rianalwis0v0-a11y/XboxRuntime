cmake_minimum_required(VERSION 3.20)

project(XboxRuntime LANGUAGES C CXX)

# Build the upstream Xbox hardware/runtime libraries as pinned source.
set(XBOXRECOMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(XBOXRECOMP_BUILD_D3D8 ON CACHE BOOL "" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xboxrecomp/CMakeLists.txt")
    message(FATAL_ERROR
        "xboxrecomp submodule is missing. Clone with: git clone --recurse-submodules <repo-url>")
endif()

add_subdirectory(third_party/xboxrecomp)
add_subdirectory(modules/nv2a)
add_subdirectory(modules/insignia_dns)
add_subdirectory(modules/xbe_loader)
add_subdirectory(modules/fatx)
add_subdirectory(modules/input)
add_subdirectory(modules/xdvdfs)

# Build the dedicated 32-bit XBE guest process and its Winsock transport.
# The host runtime may be x64, but XBE guest code must execute in a real x86
# process: its absolute guest VAs and function pointers are 32-bit.
include(ExternalProject)
ExternalProject_Add(xbe_guest32_project
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/guest32"
    BINARY_DIR "${CMAKE_BINARY_DIR}/guest32-x86"
    CMAKE_GENERATOR "Visual Studio 17 2022"
    CMAKE_GENERATOR_PLATFORM Win32
    CMAKE_ARGS
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/out"
    BUILD_ALWAYS 1
)

# Host entry points: boot.exe loads main.dll, which starts runtime modules.
add_subdirectory(main)
add_subdirectory(boot)

# Native filesystem regression test uses a generated in-memory-style FATX image.
enable_testing()
add_executable(fatx_smoke
    tests/fatx_smoke.cpp
    modules/fatx/include/fatx.cpp
)
target_include_directories(fatx_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/fatx/include"
)
target_compile_features(fatx_smoke PRIVATE cxx_std_17)
target_compile_definitions(fatx_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME fatx_smoke COMMAND fatx_smoke)

# Exercise the borrowed NV2A core through the exported XboxRuntime module ABI.
add_executable(nv2a_smoke tests/nv2a_smoke.cpp)
target_compile_features(nv2a_smoke PRIVATE cxx_std_17)
target_compile_definitions(nv2a_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(nv2a_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(nv2a_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
add_dependencies(nv2a_smoke nv2a)
add_test(NAME nv2a_smoke COMMAND nv2a_smoke)

# Exercise the pinned NV2A vertex-program interpreter independently of D3D.

# Verify NV2A microcode is translated into compilable HLSL (no GPU required).
add_executable(nv2a_hlsl_smoke tests/nv2a_hlsl_smoke.c)
target_include_directories(nv2a_hlsl_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xboxrecomp/src/d3d"
)
target_link_libraries(nv2a_hlsl_smoke PRIVATE xbox_d3d8)
target_compile_definitions(nv2a_hlsl_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME nv2a_hlsl_smoke COMMAND nv2a_hlsl_smoke)

# Exercise XDVDFS descriptor parsing, directory-tree lookup, and bounded file reads.
add_executable(xdvdfs_smoke
    tests/xdvdfs_smoke.cpp
    modules/xdvdfs/xdvdfs.cpp
)
target_include_directories(xdvdfs_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/xdvdfs")
target_compile_features(xdvdfs_smoke PRIVATE cxx_std_17)
target_compile_definitions(xdvdfs_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME xdvdfs_smoke COMMAND xdvdfs_smoke)

# Validate the original Xbox controller ABI and disconnected/invalid-user paths.
add_executable(input_smoke
    tests/input_smoke.cpp
    modules/input/input.cpp
)
target_include_directories(input_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/input")
target_compile_features(input_smoke PRIVATE cxx_std_17)
target_compile_definitions(input_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
add_test(NAME input_smoke COMMAND input_smoke)

# Verify the guest32 Winsock bridge's startup, DNS, and socket lifecycle.
add_executable(network_smoke
    tests/network_smoke.cpp
    guest32/network_compat.cpp
    guest32/xnet_compat.cpp
)
target_include_directories(network_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/guest32")
target_compile_features(network_smoke PRIVATE cxx_std_17)
target_compile_definitions(network_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(network_smoke PRIVATE ws2_32 iphlpapi)
add_test(NAME network_smoke COMMAND network_smoke)
