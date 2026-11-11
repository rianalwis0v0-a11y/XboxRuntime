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
