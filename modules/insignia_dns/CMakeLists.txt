add_library(insignia_dns MODULE
    insignia_dns.cpp
)

target_link_libraries(insignia_dns PRIVATE
    ws2_32
    winhttp
)

target_compile_definitions(insignia_dns PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(insignia_dns PRIVATE /W4 /permissive-)
endif()

set_target_properties(insignia_dns PROPERTIES
    PREFIX ""
    OUTPUT_NAME "insignia_dns"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(insignia_dns PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    )
endforeach()
