cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeFATX LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "FATX module requires MSVC on Windows.")
endif()
add_library(fatx SHARED include/fatx.cpp)
target_include_directories(fatx PRIVATE include)
target_compile_features(fatx PRIVATE cxx_std_17)
target_compile_definitions(fatx PRIVATE NOMINMAX)
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

enable_testing()
add_executable(fatx_smoke ../../tests/fatx_smoke.cpp include/fatx.cpp)
target_include_directories(fatx_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(fatx_smoke PRIVATE cxx_std_17)
target_compile_definitions(fatx_smoke PRIVATE NOMINMAX)
target_compile_options(fatx_smoke PRIVATE /W4 /permissive-)
add_test(NAME fatx_smoke COMMAND fatx_smoke)
