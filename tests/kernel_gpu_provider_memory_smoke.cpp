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
    if (!start || !stop || !readVram || !writeVram || !readRamin || !writeRamin ||
        !bindGuestPhysicalMemory) {
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

    // The provider must accept an explicitly supplied, bounded physical RAM
    // window, reject ranges outside the Xbox 128 MiB address space, and
    // support deterministic unbinding. This tests the bind contract only;
    // it does not claim that the kernel currently supplies physical RAM.
    std::vector<std::uint8_t> guestPhysicalMemory(0x4000u, 0x5Au);
    ok = ok && bindGuestPhysicalMemory(0x1000u, guestPhysicalMemory.data(),
                                       static_cast<std::uint32_t>(guestPhysicalMemory.size())) == 0;
    ok = ok && bindGuestPhysicalMemory(0x08000000u, guestPhysicalMemory.data(),
                                       static_cast<std::uint32_t>(guestPhysicalMemory.size())) != 0;
    ok = ok && bindGuestPhysicalMemory(0x07FFF000u, guestPhysicalMemory.data(),
                                       static_cast<std::uint32_t>(guestPhysicalMemory.size())) != 0;
    ok = ok && bindGuestPhysicalMemory(0u, nullptr, 0u) == 0;

    stop();
    FreeLibrary(provider);
    if (!ok) {
        std::fprintf(stderr, "NV2A provider did not use the shared VRAM/RAMIN view\n");
        return 6;
    }
    std::puts("kernel_gpu_provider_memory_smoke: PASS (shared VRAM/RAMIN and bounded guest-physical bind contract)");
    return 0;
}
