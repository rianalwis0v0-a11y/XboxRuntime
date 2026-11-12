add_library(nv2a MODULE
    nv2a_bridge.c
)

target_include_directories(nv2a PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/xboxrecomp/src/nv2a"
)

target_link_libraries(nv2a PRIVATE
    xbox_nv2a
    xbox_d3d8
)

target_compile_definitions(nv2a PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(nv2a PRIVATE /W4)
endif()

set_target_properties(nv2a PROPERTIES
    PREFIX ""
    OUTPUT_NAME "nv2a"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(nv2a PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
