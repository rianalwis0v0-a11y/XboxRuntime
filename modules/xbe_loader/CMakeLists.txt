add_library(xbe_loader MODULE xbe_loader.cpp)
target_compile_definitions(xbe_loader PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
 target_compile_options(xbe_loader PRIVATE /W4 /permissive-)
endif()
set_target_properties(xbe_loader PROPERTIES PREFIX "" OUTPUT_NAME "xbe_loader"
 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
 LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
 set_target_properties(xbe_loader PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
