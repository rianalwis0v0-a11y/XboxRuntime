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
