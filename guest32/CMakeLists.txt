cmake_minimum_required(VERSION 3.20)
project(XboxNetGuest32 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XboxNet guest networking currently requires Windows and MSVC.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "XboxNet guest ABI must be built with the Win32/x86 toolchain.")
endif()

enable_testing()
add_executable(network_smoke
  ../tests/network_smoke.cpp
  network_compat.cpp
  xnet_compat.cpp
  xonline_compat.cpp
)
target_compile_features(network_smoke PRIVATE cxx_std_17)
target_include_directories(network_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(network_smoke PRIVATE /W4 /permissive-)
target_link_libraries(network_smoke PRIVATE ws2_32 iphlpapi bcrypt)
add_test(NAME network_smoke COMMAND network_smoke)
