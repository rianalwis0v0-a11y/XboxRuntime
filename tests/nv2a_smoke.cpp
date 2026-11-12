#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using StartFn = int (__cdecl*)();
using StopFn = void (__cdecl*)();
using ReadFn = uint64_t (__cdecl*)(uint32_t, unsigned int);
using WriteFn = int (__cdecl*)(uint32_t, uint64_t, unsigned int);
using SubmitFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t);
using FlushFn = void (__cdecl*)();
struct Stats {
    uint32_t frames;
    uint32_t draw_calls;
    uint32_t vertices_submitted;
    uint32_t methods_handled;
    uint32_t methods_ignored;
    uint32_t clears;
};
using StatsFn = int (__cdecl*)(Stats*);

using ReadMemoryFn = int (__cdecl*)(uint32_t, void*, uint32_t);
using WriteMemoryFn = int (__cdecl*)(uint32_t, const void*, uint32_t);
using BatchFn = int (__cdecl*)(uint32_t, const uint32_t*, uint32_t);
using PushBufferFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*);


static FARPROC Need(HMODULE module, const char* name) {
    FARPROC p = GetProcAddress(module, name);
    if (!p) {
        std::cerr << "FAIL: missing nv2a.dll export " << name << "\n";
        ExitProcess(10);
    }
    return p;
}

int main() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) {
        std::cerr << "FAIL: cannot locate smoke-test executable\n";
        return 1;
    }
    std::wstring dll(path.data(), n);
    const size_t slash = dll.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return 1;
    dll.resize(slash + 1);
    dll += L"nv2a.dll";

    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        std::cerr << "FAIL: LoadLibrary(nv2a.dll) error=" << GetLastError() << "\n";
        return 1;
    }

    auto start = reinterpret_cast<StartFn>(Need(module, "ModuleStart"));
    auto stop = reinterpret_cast<StopFn>(Need(module, "ModuleStop"));
    auto read = reinterpret_cast<ReadFn>(Need(module, "XboxNV2A_ReadRegister"));
    auto write = reinterpret_cast<WriteFn>(Need(module, "XboxNV2A_WriteRegister"));
    auto submit = reinterpret_cast<SubmitFn>(Need(module, "XboxNV2A_SubmitMethod"));
    auto flush = reinterpret_cast<FlushFn>(Need(module, "XboxNV2A_Flush"));
    auto getStats = reinterpret_cast<StatsFn>(Need(module, "XboxNV2A_GetStats"));

    auto readVram = reinterpret_cast<ReadMemoryFn>(Need(module, "XboxNV2A_ReadVRAM"));
    auto writeVram = reinterpret_cast<WriteMemoryFn>(Need(module, "XboxNV2A_WriteVRAM"));
    auto readRamin = reinterpret_cast<ReadMemoryFn>(Need(module, "XboxNV2A_ReadRAMIN"));
    auto writeRamin = reinterpret_cast<WriteMemoryFn>(Need(module, "XboxNV2A_WriteRAMIN"));
    auto submitBatch = reinterpret_cast<BatchFn>(Need(module, "XboxNV2A_SubmitMethods"));
    auto submitPushBuffer = reinterpret_cast<PushBufferFn>(Need(module, "XboxNV2A_SubmitPushBuffer"));


    if (start() != 0) {
        std::cerr << "FAIL: NV2A module initialization\n";
        return 1;
    }


    const uint32_t pattern[4] = {0x11223344u, 0x55667788u, 0xAABBCCDDu, 0x01020304u};
    uint32_t copy[4]{};
    if (writeVram(0x1000, pattern, sizeof(pattern)) != 0 ||
        readVram(0x1000, copy, sizeof(copy)) != 0 ||
        memcmp(pattern, copy, sizeof(pattern)) != 0) {
        std::cerr << "FAIL: VRAM read/write round trip\\n";
        stop();
        return 1;
    }
    if (writeRamin(0x80, pattern, sizeof(pattern)) != 0 ||
        readRamin(0x80, copy, sizeof(copy)) != 0 ||
        memcmp(pattern, copy, sizeof(pattern)) != 0) {
        std::cerr << "FAIL: RAMIN read/write round trip\\n";
        stop();
        return 1;
    }
    if (writeVram(64u * 1024u * 1024u - 2u, pattern, sizeof(pattern)) == 0 ||
        readRamin(1024u * 1024u, copy, 1) == 0) {
        std::cerr << "FAIL: GPU memory bounds were not enforced\\n";
        stop();
        return 1;
    }
    const uint32_t batch[] = {0x01D4, 0xFF223344, 0x17FC, 5, 0x17FC, 0};
    if (submitBatch(0, batch, 3) != 0 || submitBatch(8, batch, 3) == 0) {
        std::cerr << "FAIL: PGRAPH batch validation/dispatch\\n";
        stop();
        return 1;
    }

    /* Exercise real NV4-style pushbuffer packet decoding from VRAM. */
    const uint32_t pushbuffer[] = {
        0x40000000u | (2u << 18) | 0x17FCu, 5u, 0u, /* non-incrementing */
        (1u << 18) | 0x01D4u, 0xFF223344u          /* incrementing */
    };
    uint32_t methodsSubmitted = 0;
    if (writeVram(0x2000, pushbuffer, sizeof(pushbuffer)) != 0 ||
        submitPushBuffer(0x2000, sizeof(pushbuffer), &methodsSubmitted) != 0 ||
        methodsSubmitted != 3) {
        std::cerr << "FAIL: NV2A pushbuffer packet decode/dispatch\n";
        stop();
        return 1;
    }
    const uint32_t malformedPushbuffer[] = { 0x00000001u };
    if (writeVram(0x2100, malformedPushbuffer, sizeof(malformedPushbuffer)) != 0 ||
        submitPushBuffer(0x2100, sizeof(malformedPushbuffer), &methodsSubmitted) == 0 ||
        methodsSubmitted != 0) {
        std::cerr << "FAIL: malformed NV2A pushbuffer was accepted\n";
        stop();
        return 1;
    }

    const uint64_t boot = read(0x00000000, 4);
    if (boot != 0x02A000A3u) {
        std::cerr << "FAIL: PMC_BOOT_0 expected 0x02A000A3 got 0x"
                  << std::hex << boot << "\n";
        stop();
        return 1;
    }

    const uint64_t pci = read(0x001800, 4);
    if (pci != 0x02A010DEu) {
        std::cerr << "FAIL: NV2A PCI identity expected 0x02A010DE got 0x"
                  << std::hex << pci << "\n";
        stop();
        return 1;
    }

    if (write(0x000140, 1, 4) != 0 || read(0x000140, 4) != 1) {
        std::cerr << "FAIL: PMC interrupt-enable MMIO write/read\n";
        stop();
        return 1;
    }
    if (write(0x000140, 0, 3) != 2) {
        std::cerr << "FAIL: invalid MMIO access size was not rejected\n";
        stop();
        return 1;
    }

    // Exercise PGRAPH method dispatch without requiring a window or a D3D device.
    if (submit(0, 0x01D4, 0xFF112233) != 0 ||
        submit(0, 0x17FC, 5) != 0 ||
        submit(0, 0x17FC, 0) != 0) {
        std::cerr << "FAIL: PGRAPH method submission\n";
        stop();
        return 1;
    }
    flush();

    Stats stats{};
    if (getStats(&stats) != 0 || stats.methods_handled < 3) {
        std::cerr << "FAIL: PGRAPH statistics did not record submitted methods\n";
        stop();
        return 1;
    }

    stop();
    std::cout << "PASS: NV2A module init, PMC/PBUS MMIO, access validation, "
                 "PGRAPH method dispatch, and statistics\n";
    std::cout << "LIMIT: no D3D device or rendered frame was tested\n";
    FreeLibrary(module);
    return 0;
}
