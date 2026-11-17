cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeFATX LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "FATX module requires MSVC on Windows.")
endif()
add_library(fatx SHARED include/fatx.cpp)
target_include_directories(fatx PRIVATE include)
target_compile_features(fatx PRIVATE cxx_std_17)
target_compile_definitions(fatx PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(fatx PRIVATE /W4 /permissive-)
set_target_properties(fatx PROPERTIES
  OUTPUT_NAME "fatx"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(fatx PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
