cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeMain LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XboxRuntime main module requires MSVC on Windows.")
endif()
add_library(main SHARED main.cpp main.h ../modules/xbe_loader/xbe_backend_api.h)
target_include_directories(main PRIVATE ..)
target_compile_features(main PRIVATE cxx_std_17)
target_compile_definitions(main PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(main PRIVATE /W4 /permissive-)
set_target_properties(main PROPERTIES
  OUTPUT_NAME "main"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
