add_library(xdvdfs MODULE
    xdvdfs.cpp
)
target_include_directories(xdvdfs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(xdvdfs PRIVATE cxx_std_17)
target_compile_definitions(xdvdfs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(xdvdfs PRIVATE /W4 /permissive-)
endif()
set_target_properties(xdvdfs PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xdvdfs"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(xdvdfs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
