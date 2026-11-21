cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeNv2aProvider LANGUAGES C CXX)

# Build the reviewed, pinned NV2A implementation as a provider DLL that the
# combined kernel backend loads explicitly. Keep this project standalone so
# it can be built by the kernel integration workflow.
set(XBOXRECOMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(XBOXRECOMP_BUILD_D3D8 ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../third_party/xboxrecomp"
                 "${CMAKE_CURRENT_BINARY_DIR}/xboxrecomp")

add_library(nv2a MODULE
    nv2a_bridge.c
    nv2a_vertex_shader.c
    nv2a.def
)
target_include_directories(nv2a PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp/src/nv2a"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp/src/d3d"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp/src/kernel"
)
target_link_libraries(nv2a PRIVATE xbox_nv2a xbox_d3d8 user32)
target_compile_definitions(nv2a PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(nv2a PRIVATE /W4 /permissive-)
endif()
set_target_properties(nv2a PROPERTIES
    PREFIX "" OUTPUT_NAME "nv2a"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(nv2a PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()

# Keep the NV2A microcode validator's safety contract executable in CI.
include(CTest)
if(BUILD_TESTING)
    add_executable(nv2a_vertex_shader_smoke
        ../../tests/nv2a_vertex_shader_smoke.cpp
        nv2a_vertex_shader.c
    )
    target_include_directories(nv2a_vertex_shader_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions(nv2a_vertex_shader_smoke PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX
    )
    if(MSVC)
        target_compile_options(nv2a_vertex_shader_smoke PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME nv2a_vertex_shader_smoke COMMAND nv2a_vertex_shader_smoke)
endif()
