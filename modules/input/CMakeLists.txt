add_library(input MODULE
    input.cpp
)

target_include_directories(input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(input PRIVATE cxx_std_17)
target_compile_definitions(input PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(input PRIVATE /W4 /permissive-)
endif()

set_target_properties(input PROPERTIES
    PREFIX ""
    OUTPUT_NAME "input"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(input PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
