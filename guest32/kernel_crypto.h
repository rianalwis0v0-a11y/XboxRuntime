#pragma once
#include <cstdint>

extern "C" {
void __stdcall K_XcSHAInit(std::uint8_t* context);
void __stdcall K_XcSHAUpdate(std::uint8_t* context, std::uint8_t* input, std::uint32_t length);
void __stdcall K_XcSHAFinal(std::uint8_t* context, std::uint8_t* digest);
void __stdcall K_XcRC4Key(std::uint8_t* keyStruct, std::uint32_t keyLength, std::uint8_t* key);
void __stdcall K_XcRC4Crypt(std::uint8_t* keyStruct, std::uint32_t inputLength, std::uint8_t* input);
void __stdcall K_XcHMAC(std::uint8_t* key, std::uint32_t keyLength,
                        std::uint8_t* input1, std::uint32_t input1Length,
                        std::uint8_t* input2, std::uint32_t input2Length,
                        std::uint8_t* digest);
std::uint32_t __stdcall K_XcCryptService(std::uint32_t operation, void* arguments);
void __stdcall K_XcDESKeyParity(std::uint8_t* key, std::uint32_t keyLength);
}
