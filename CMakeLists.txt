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
add_subdirectory(modules/audio)
add_subdirectory(modules/xdvdfs)
add_subdirectory(guest32)

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

# Validate the audio module ABI and invalid-input handling without requiring
# a physical audio device in the CI runner.
add_executable(audio_smoke
    tests/audio_smoke.cpp
    modules/audio/audio.cpp
)
target_include_directories(audio_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/audio")
target_compile_features(audio_smoke PRIVATE cxx_std_17)
target_compile_definitions(audio_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(audio_smoke PRIVATE winmm)
add_test(NAME audio_smoke COMMAND audio_smoke)


// Deploy the runnable host and its native guest/runtime DLLs as one layout.
// boot.exe lives at XboxRuntime\\boot.exe; all DLLs plus xbe_guest32.exe
// live in XboxRuntime\\system so both the host and XBE loader resolve them
// relative to the same deployment directory.
install(TARGETS boot
    RUNTIME DESTINATION .
)
install(TARGETS main nv2a insignia_dns xbe_loader fatx input audio xdvdfs xbe_guest32
    RUNTIME DESTINATION system
    LIBRARY DESTINATION system
)
