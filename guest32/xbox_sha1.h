#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t XBOX_SHA_CONTEXT_BYTES = 24u + 92u;

extern "C" void __stdcall K_XcSHAInit(std::uint8_t* context);
extern "C" void __stdcall K_XcSHAUpdate(std::uint8_t* context,
                                         const std::uint8_t* input,
                                         std::uint32_t length);
extern "C" void __stdcall K_XcSHAFinal(std::uint8_t* context,
                                        std::uint8_t* digest);
