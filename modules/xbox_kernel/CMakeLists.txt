cmake_minimum_required(VERSION 3.20)
project(XboxKernelBackend LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XboxKernel backend requires MSVC on Windows.")
endif()
add_library(xbox_kernel SHARED xbox_kernel.cpp xbox_kernel.def)
target_compile_features(xbox_kernel PRIVATE cxx_std_17)
target_compile_definitions(xbox_kernel PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(xbox_kernel PRIVATE /W4 /permissive-)
target_include_directories(xbox_kernel PRIVATE ../xbe_loader ../../guest32)
set_target_properties(xbox_kernel PROPERTIES
  OUTPUT_NAME "xbox_kernel"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(xbox_kernel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out"
    LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()

enable_testing()
add_executable(kernel_guest_backend_smoke ../../tests/kernel_guest_backend_smoke.cpp)
target_compile_features(kernel_guest_backend_smoke PRIVATE cxx_std_17)
target_compile_definitions(kernel_guest_backend_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kernel_guest_backend_smoke PRIVATE /W4 /permissive-)
target_include_directories(kernel_guest_backend_smoke PRIVATE ../xbe_loader)
set_target_properties(kernel_guest_backend_smoke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(kernel_guest_backend_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()
add_test(NAME kernel_guest_backend_smoke COMMAND kernel_guest_backend_smoke)
