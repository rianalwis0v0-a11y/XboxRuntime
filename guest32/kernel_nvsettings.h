#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>

using XboxNvStatus = std::int32_t;

struct XboxNvSettingInfo {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t type;
    bool writable;
};

bool XboxNvInitialize(const wchar_t* path);
void XboxNvFlush();
bool XboxNvReadRaw(std::uint32_t offset, void* buffer, std::uint32_t length);
bool XboxNvWriteRaw(std::uint32_t offset, const void* buffer, std::uint32_t length);
bool XboxNvGetSettingInfo(std::uint32_t index, XboxNvSettingInfo* info);
std::uint32_t XboxNvComputeChecksum(const std::uint8_t* bytes, std::size_t length);

extern "C" XboxNvStatus __stdcall K_ExQueryNonVolatileSetting(
    std::uint32_t valueIndex, std::uint32_t* type, void* value,
    std::uint32_t valueLength, std::uint32_t* resultLength);
extern "C" XboxNvStatus __stdcall K_ExSaveNonVolatileSetting(
    std::uint32_t valueIndex, std::uint32_t type, const void* value,
    std::uint32_t valueLength);

struct XboxRefurbInfo32 {
    std::uint32_t signature;
    std::uint32_t powerCycleCount;
    std::int64_t firstSetTime;
};
static_assert(sizeof(XboxRefurbInfo32) == 16, "XBOX_REFURB_INFO must match pinned NXDK x86 layout");

extern "C" XboxNvStatus __stdcall K_ExReadWriteRefurbInfo(
    XboxRefurbInfo32* refurbInfo, std::uint32_t valueLength, std::uint8_t doWrite);
extern "C" std::uint32_t __stdcall K_AvSetDisplayMode(
    void* registerBase, std::uint32_t step, std::uint32_t displayMode,
    std::uint32_t sourceColorFormat, std::uint32_t pitch, std::uint32_t frameBuffer);
