#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
std::uint32_t __cdecl XboxGuestApiLookup(const char* symbolName);
std::uint32_t __cdecl XboxGuestApiCount();
bool __cdecl XboxGuestResolveNonKernelImports(std::uint32_t imageBase, std::uint32_t imageSize, std::uint32_t directoryAddress);
#ifdef __cplusplus
}
#endif
