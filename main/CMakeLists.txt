add_library(main SHARED
    main.cpp
    main.h
)
target_compile_features(main PRIVATE cxx_std_17)
target_compile_definitions(main PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(main PROPERTIES
    OUTPUT_NAME "main"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(main PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
