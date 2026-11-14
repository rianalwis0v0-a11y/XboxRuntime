cmake_minimum_required(VERSION 3.20)
project(XboxGuest32 LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "XboxGuest32 currently requires the MSVC Windows toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "xbe_guest32 must be configured with the Win32/x86 toolchain.")
endif()
add_executable(xbe_guest32 guest32.cpp kernel_compat.cpp xbox_sha1.cpp kernel_memory.cpp kernel_object_manager.cpp network_compat.cpp xnet_compat.cpp guest_api_registry.cpp)
target_compile_features(xbe_guest32 PRIVATE cxx_std_17)
target_compile_definitions(xbe_guest32 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(xbe_guest32 PRIVATE /W4 /permissive-)
target_link_libraries(xbe_guest32 PRIVATE ws2_32 iphlpapi)
target_link_options(xbe_guest32 PRIVATE /BASE:0x70000000 /DYNAMICBASE:NO)
target_link_options(xbe_guest32 PRIVATE /LARGEADDRESSAWARE)
set_target_properties(xbe_guest32 PROPERTIES OUTPUT_NAME "xbe_guest32"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(xbe_guest32 PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/out")
endforeach()


# Run the XNet/XSocket transport contract tests in the same x86 toolchain.
enable_testing()
add_executable(network_smoke
  ../tests/network_smoke.cpp
  network_compat.cpp
  xnet_compat.cpp
)
target_include_directories(network_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(network_smoke PRIVATE cxx_std_17)
target_compile_definitions(network_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(network_smoke PRIVATE /W4 /permissive-)
target_link_libraries(network_smoke PRIVATE ws2_32 iphlpapi)
add_test(NAME network_smoke COMMAND network_smoke)

# Verify the name-based API registry returns callable x86 XNet/XSocket entries.
add_executable(guest_api_registry_smoke
  ../tests/guest_api_registry_smoke.cpp
  guest_api_registry.cpp
  network_compat.cpp
  xnet_compat.cpp
)
target_include_directories(guest_api_registry_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(guest_api_registry_smoke PRIVATE cxx_std_17)
target_compile_definitions(guest_api_registry_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(guest_api_registry_smoke PRIVATE /W4 /permissive-)
target_link_libraries(guest_api_registry_smoke PRIVATE ws2_32 iphlpapi)
add_test(NAME guest_api_registry_smoke COMMAND guest_api_registry_smoke)

# Execute the x86 guest-facing memory/object entry points with their real stdcall ABI.
add_executable(kernel_memory_guest_smoke
  ../tests/kernel_memory_guest_smoke.cpp
  kernel_memory.cpp
  kernel_object_manager.cpp
)
target_include_directories(kernel_memory_guest_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(kernel_memory_guest_smoke PRIVATE cxx_std_17)
target_compile_definitions(kernel_memory_guest_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kernel_memory_guest_smoke PRIVATE /W4 /permissive-)
add_test(NAME kernel_memory_guest_smoke COMMAND kernel_memory_guest_smoke)

add_executable(kernel_handle_guest_smoke
  ../tests/kernel_handle_guest_smoke.cpp
  kernel_memory.cpp
  kernel_object_manager.cpp
)
target_include_directories(kernel_handle_guest_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(kernel_handle_guest_smoke PRIVATE cxx_std_17)
target_compile_definitions(kernel_handle_guest_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(kernel_handle_guest_smoke PRIVATE /W4 /permissive-)
add_test(NAME kernel_handle_guest_smoke COMMAND kernel_handle_guest_smoke)
