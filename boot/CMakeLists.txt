add_executable(boot WIN32
    boot.cpp
    boot.h
)
target_compile_features(boot PRIVATE cxx_std_17)
target_compile_definitions(boot PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(boot PRIVATE comctl32 winmm)
set_target_properties(boot PROPERTIES
    OUTPUT_NAME "boot"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(boot PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
