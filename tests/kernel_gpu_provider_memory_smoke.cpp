#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../guest32/kernel_gpu_service_protocol.h"

using StartWithMemoryFn = int (__cdecl*)(void*, std::uint32_t);
using StopFn = void (__cdecl*)();
using MemoryReadFn = int (__cdecl*)(std::uint32_t, void*, std::uint32_t);
using MemoryWriteFn = int (__cdecl*)(std::uint32_t, const void*, std::uint32_t);
using BindGuestPhysicalMemoryFn = int (__cdecl*)(std::uint32_t, void*, std::uint32_t);
using WriteRegisterFn = int (__cdecl*)(std::uint32_t, std::uint64_t, unsigned int);
using ResolveDmaHandleFn = int (__cdecl*)(std::uint32_t, std::uint32_t, void*);
using SubmitDmaPushBufferFn = int (__cdecl*)(std::uint32_t, std::uint32_t,
    std::uint32_t, std::uint32_t, std::uint32_t*, std::uint32_t*);

struct DmaInfo {
    std::uint32_t handle;
    std::uint32_t instance_offset;
    std::uint32_t dma_class;
    std::uint32_t dma_target;
    std::uint32_t engine;
    std::uint32_t base_address;
    std::uint32_t limit;
    std::uint32_t mapped_length;
};
static_assert(sizeof(DmaInfo) == 32, "NV2A DMA info ABI drift");

int wmain() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, _countof(executable));
    if (!length || length >= _countof(executable)) return 1;
    std::wstring directory(executable, length);
    const auto slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return 2;
    directory.resize(slash);
    const std::wstring providerPath = directory + L"\\nv2a.dll";
    HMODULE provider = LoadLibraryExW(providerPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!provider) {
        std::fwprintf(stderr, L"could not load NV2A provider: %lu\n", GetLastError());
        return 3;
    }

    const auto start = reinterpret_cast<StartWithMemoryFn>(
        GetProcAddress(provider, "ModuleStartWithMemory"));
    const auto stop = reinterpret_cast<StopFn>(GetProcAddress(provider, "ModuleStop"));
    const auto readVram = reinterpret_cast<MemoryReadFn>(
        GetProcAddress(provider, "XboxNV2A_ReadVRAM"));
    const auto writeVram = reinterpret_cast<MemoryWriteFn>(
        GetProcAddress(provider, "XboxNV2A_WriteVRAM"));
    const auto readRamin = reinterpret_cast<MemoryReadFn>(
        GetProcAddress(provider, "XboxNV2A_ReadRAMIN"));
    const auto writeRamin = reinterpret_cast<MemoryWriteFn>(
        GetProcAddress(provider, "XboxNV2A_WriteRAMIN"));
    const auto bindGuestPhysicalMemory =
        reinterpret_cast<BindGuestPhysicalMemoryFn>(
            GetProcAddress(provider, "XboxNV2A_BindGuestPhysicalMemory"));
    const auto writeRegister = reinterpret_cast<WriteRegisterFn>(
        GetProcAddress(provider, "XboxNV2A_WriteRegister"));
    const auto resolveDmaHandle = reinterpret_cast<ResolveDmaHandleFn>(
        GetProcAddress(provider, "XboxNV2A_ResolveDMAHandle"));
    const auto submitDmaPushBuffer = reinterpret_cast<SubmitDmaPushBufferFn>(
        GetProcAddress(provider, "XboxNV2A_SubmitDMAPushBuffer"));
    if (!start || !stop || !readVram || !writeVram || !readRamin || !writeRamin ||
        !bindGuestPhysicalMemory || !writeRegister || !resolveDmaHandle ||
        !submitDmaPushBuffer) {
        FreeLibrary(provider);
        return 4;
    }

    std::vector<std::uint8_t> sharedMemory(XBOX_GPU_SHARED_MEMORY_SIZE, 0);
    if (start(sharedMemory.data(), XBOX_GPU_SHARED_MEMORY_SIZE) != 0) {
        FreeLibrary(provider);
        return 5;
    }

    const std::uint8_t vramSeed[] = {0x11, 0x22, 0x33, 0x44};
    const std::uint8_t raminSeed[] = {0xA1, 0xB2, 0xC3, 0xD4};
    const std::uint8_t update[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::uint8_t readback[4]{};
    std::memcpy(sharedMemory.data() + 0x100u, vramSeed, sizeof(vramSeed));
    std::memcpy(sharedMemory.data() + XBOX_GPU_SHARED_VRAM_SIZE + 0x200u,
                raminSeed, sizeof(raminSeed));

    bool ok = readVram(0x100u, readback, sizeof(readback)) == 0 &&
              std::memcmp(readback, vramSeed, sizeof(readback)) == 0;
    std::memset(readback, 0, sizeof(readback));
    ok = ok && readRamin(0x200u, readback, sizeof(readback)) == 0 &&
         std::memcmp(readback, raminSeed, sizeof(readback)) == 0;
    ok = ok && writeVram(0x300u, update, sizeof(update)) == 0 &&
         std::memcmp(sharedMemory.data() + 0x300u, update, sizeof(update)) == 0;
    ok = ok && writeRamin(0x400u, update, sizeof(update)) == 0 &&
         std::memcmp(sharedMemory.data() + XBOX_GPU_SHARED_VRAM_SIZE + 0x400u,
                     update, sizeof(update)) == 0;

    // Mirror the kernel's dedicated 64 MiB guest physical RAM section and
    // prove the provider actually fetches a pushbuffer from its PCI/AGP DMA
    // mapping, rather than merely accepting a bind call.
    constexpr std::uint32_t guestRamBytes = 64u * 1024u * 1024u;
    constexpr std::uint32_t dmaHandle = 0x12345678u;
    constexpr std::uint32_t dmaInstance = 0x1000u;
    constexpr std::uint32_t pushbufferPhysical = 0x2000u;
    std::vector<std::uint8_t> guestPhysicalMemory(guestRamBytes, 0);
    ok = ok && bindGuestPhysicalMemory(0u, guestPhysicalMemory.data(),
                                       guestRamBytes) == 0;
    ok = ok && bindGuestPhysicalMemory(0x08000000u, guestPhysicalMemory.data(),
                                       guestRamBytes) != 0;
    ok = ok && bindGuestPhysicalMemory(0x07FFF000u, guestPhysicalMemory.data(),
                                       guestRamBytes) != 0;

    // RAMHT: 4 KiB table at RAMIN offset zero, 16-entry search window.
    // The table's 9-bit handle hash is the same fold used by the provider.
    ok = ok && writeRegister(0x2210u, 0u, 4u) == 0;
    std::uint32_t hash = 0;
    for (std::uint32_t value = dmaHandle; value; value >>= 9)
        hash ^= value & 0x1FFu;
    hash &= 0x1FFu;
    const std::uint32_t ramhtEntry[2] = {
        dmaHandle, 0x80010000u | (dmaInstance >> 4)
    };
    const std::uint32_t dmaObject[3] = {
        0x00010002u, 0x100u, pushbufferPhysical
    };
    const std::uint32_t pushbuffer[2] = {
        (1u << 18) | (0x50u / 4u << 2), 0x12345678u
    };
    ok = ok && writeRamin(hash * 8u, ramhtEntry, sizeof(ramhtEntry)) == 0;
    ok = ok && writeRamin(dmaInstance, dmaObject, sizeof(dmaObject)) == 0;
    std::memcpy(guestPhysicalMemory.data() + pushbufferPhysical,
                pushbuffer, sizeof(pushbuffer));

    DmaInfo dma{};
    ok = ok && resolveDmaHandle(0u, dmaHandle, &dma) == 0 &&
         dma.dma_target == 0x00010000u &&
         dma.base_address == pushbufferPhysical &&
         dma.mapped_length == 0x100u;
    std::uint32_t newGet = 0, methodsSubmitted = 0;
    ok = ok && submitDmaPushBuffer(0u, dmaHandle, 0u, sizeof(pushbuffer),
                                    &newGet, &methodsSubmitted) == 0 &&
         newGet == sizeof(pushbuffer) && methodsSubmitted == 1u;

    // A DMA object's span must be rejected if its physical limit crosses
    // the end of the bound guest RAM section.
    const std::uint32_t invalidDmaObject[3] = {
        0x00010002u, 0x100u, guestRamBytes - 0x10u
    };
    ok = ok && writeRamin(dmaInstance, invalidDmaObject,
                          sizeof(invalidDmaObject)) == 0;
    ok = ok && resolveDmaHandle(0u, dmaHandle, &dma) != 0;
    ok = ok && bindGuestPhysicalMemory(0u, nullptr, 0u) == 0;
    ok = ok && resolveDmaHandle(0u, dmaHandle, &dma) != 0;

    stop();
    FreeLibrary(provider);
    if (!ok) {
        std::fprintf(stderr, "NV2A provider failed shared-memory or real guest-RAM DMA validation\n");
        return 6;
    }
    std::puts("kernel_gpu_provider_memory_smoke: PASS (shared VRAM/RAMIN, guest physical RAM DMA pushbuffer, and bounds)");
    return 0;
}
