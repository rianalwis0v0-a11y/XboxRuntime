cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeStorage LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XboxRuntime storage module requires MSVC on Windows.")
endif()
add_library(xbox_storage SHARED xbox_storage.cpp)
target_compile_features(xbox_storage PRIVATE cxx_std_17)
target_compile_definitions(xbox_storage PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(xbox_storage PRIVATE /W4 /permissive-)
set_target_properties(xbox_storage PROPERTIES
  OUTPUT_NAME "xbox_storage"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(xbox_storage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
