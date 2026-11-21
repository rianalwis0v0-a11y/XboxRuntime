#pragma once
#include <windows.h>
#include <cstdint>

// Guest physical RAM is a dedicated shared section, independent of the XBE
// image mapping and the NV2A-local VRAM/RAMIN section.
constexpr std::uint32_t XBOX_GUEST_PHYSICAL_RAM_BASE = 0x00000000u;
constexpr std::uint32_t XBOX_GUEST_PHYSICAL_RAM_SIZE = 64u * 1024u * 1024u;

enum XboxGuestMemoryAccess : ULONG {
    XboxGuestMemoryRead = 1u,
    XboxGuestMemoryWrite = 2u
};

extern "C" {
BOOL __stdcall XboxKernelPhysicalMemoryInitialize(
    void* mappedView, std::uint32_t physicalBase, std::uint32_t length);
void __stdcall XboxKernelPhysicalMemoryShutdown();
BOOL __stdcall XboxKernelTranslateGuestVirtualToPhysical(
    const void* guestAddress, SIZE_T length, ULONG access,
    std::uint32_t* physicalAddress);
void* __stdcall XboxKernelGetPhysicalMemoryView(
    std::uint32_t* physicalBase, std::uint32_t* length);
void* __stdcall XboxKernelAllocatePhysicalMemory(
    std::uint32_t bytes, std::uint32_t alignment, std::uint32_t maximumAddress,
    std::uint32_t* physicalAddress);
BOOL __stdcall XboxKernelFreePhysicalMemory(void* allocation);
}
