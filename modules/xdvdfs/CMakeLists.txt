cmake_minimum_required(VERSION 3.20)
project(XboxRuntimeXDVDFS LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XDVDFS module requires MSVC on Windows.")
endif()
add_library(xdvdfs SHARED xdvdfs.cpp)
target_include_directories(xdvdfs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(xdvdfs PRIVATE cxx_std_17)
target_compile_definitions(xdvdfs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(xdvdfs PRIVATE /W4 /permissive-)
set_target_properties(xdvdfs PROPERTIES
  OUTPUT_NAME "xdvdfs"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(xdvdfs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
enable_testing()
add_executable(xdvdfs_smoke ../../tests/xdvdfs_smoke.cpp xdvdfs.cpp)
target_include_directories(xdvdfs_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(xdvdfs_smoke PRIVATE cxx_std_17)
target_compile_definitions(xdvdfs_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(xdvdfs_smoke PRIVATE /W4 /permissive-)
add_test(NAME xdvdfs_smoke COMMAND xdvdfs_smoke)
