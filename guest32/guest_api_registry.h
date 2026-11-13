#pragma once
#include <cstdint>

// Name-based guest32 API lookup. Addresses returned here are valid only in
// the x86 xbe_guest32 process; never pass them to the x64 host or treat them
// as kernel ordinals. The registry is an internal runtime dispatch surface.
#ifdef __cplusplus
extern "C" {
#endif
std::uint32_t __cdecl XboxGuestApiLookup(const char* symbolName);
std::uint32_t __cdecl XboxGuestApiCount();
#ifdef __cplusplus
}
#endif
