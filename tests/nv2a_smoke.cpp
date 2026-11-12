#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
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

    if (start() != 0) {
        std::cerr << "FAIL: NV2A module initialization\n";
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
