#pragma once
#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif
// Returns a 32-bit address valid only in xbe_guest32. Zero means unknown or unavailable.
std::uint32_t __cdecl XboxKernelGetGuestApiAddress(const char* symbolName);
#ifdef __cplusplus
}
#endif
