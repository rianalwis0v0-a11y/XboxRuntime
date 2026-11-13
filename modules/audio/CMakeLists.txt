add_library(audio MODULE audio.cpp)
target_include_directories(audio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(audio PRIVATE cxx_std_17)
target_compile_definitions(audio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(audio PRIVATE winmm)
if(MSVC)
    target_compile_options(audio PRIVATE /W4 /permissive-)
endif()
set_target_properties(audio PROPERTIES
    PREFIX ""
    OUTPUT_NAME "audio"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(audio PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
