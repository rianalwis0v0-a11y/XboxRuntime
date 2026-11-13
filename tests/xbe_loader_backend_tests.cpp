#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../modules/xbe_loader/xbe_backend_api.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

using ModuleStart = int (__cdecl*)();
using ModuleStop = void (__cdecl*)();
using RegisterBackend = int (__cdecl*)(const XBE_EXECUTION_BACKEND*);
using LoadXbe = int (__cdecl*)(const wchar_t*);
using UnloadXbe = void (__cdecl*)();

static bool g_reserved = false;
static bool g_importsResolved = false;
static bool g_guestStarted = false;
static bool g_guestStopped = false;
static bool g_released = false;
static int g_sequence = 0;
static int g_reserveOrder = 0;
static int g_resolveOrder = 0;
static int g_startOrder = 0;
static int g_stopOrder = 0;
static int g_releaseOrder = 0;

static void Put32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                  std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

static int __cdecl ReserveImage(std::uint32_t base, std::uint32_t size,
                                std::uint8_t** image, void*) {
    if (base != 0x10000 || !image || size != 0x2000) return 1;
    *image = new (std::nothrow) std::uint8_t[size];
    if (!*image) return 2;
    g_reserved = true;
    g_reserveOrder = ++g_sequence;
    return 0;
}

static void __cdecl ReleaseImage(std::uint8_t* image, void*) {
    delete[] image;
    g_released = true;
    g_releaseOrder = ++g_sequence;
}

static int __cdecl ResolveImports(std::uint8_t* image,
                                  const XBE_IMAGE_INFO* info, void*) {
    if (!image || !info || info->entryPoint != 0x11010 ||
        info->sectionCount != 1 || image[0x1000] != 0xA5)
        return 1;
    g_importsResolved = true;
    g_resolveOrder = ++g_sequence;
    return 0;
}

static int __cdecl StartGuest(std::uint32_t entry, std::uint32_t tls, void*) {
    if (entry != 0x11010 || tls != 0) return 1;
    g_guestStarted = true;
    g_startOrder = ++g_sequence;
    return 0;
}

static void __cdecl StopGuest(void*) {
    g_guestStopped = true;
    g_stopOrder = ++g_sequence;
}

static bool WriteXbe(const wchar_t* path) {
    std::vector<std::uint8_t> bytes(0x600, 0);
    Put32(bytes, 0x000, 0x48454258); // XBEH
    Put32(bytes, 0x104, 0x10000);
    Put32(bytes, 0x108, 0x400);
    Put32(bytes, 0x10C, 0x2000);
    Put32(bytes, 0x110, 0x178);
    Put32(bytes, 0x118, 0x10178);
    Put32(bytes, 0x11C, 1);
    Put32(bytes, 0x120, 0x10348);
    Put32(bytes, 0x128, 0x11010 ^ 0xA8FC57AB);
    Put32(bytes, 0x158, 0x5B6D40B6); // encoded null thunk table
    Put32(bytes, 0x178, 0x1D0);
    Put32(bytes, 0x348, 0x04);       // executable section
    Put32(bytes, 0x34C, 0x11000);
    Put32(bytes, 0x350, 0x1000);
    Put32(bytes, 0x354, 0x400);
    Put32(bytes, 0x358, 0x200);
    std::memset(bytes.data() + 0x400, 0xA5, 0x200);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(),
                              static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

int wmain() {
    const wchar_t* path = L"xbe_loader_backend_test.xbe";
    assert(WriteXbe(path));

    HMODULE dll = LoadLibraryW(L"xbe_loader.dll");
    assert(dll != nullptr);

    auto start = reinterpret_cast<ModuleStart>(
        GetProcAddress(dll, "ModuleStart"));
    auto stop = reinterpret_cast<ModuleStop>(
        GetProcAddress(dll, "ModuleStop"));
    auto registerBackend = reinterpret_cast<RegisterBackend>(
        GetProcAddress(dll, "XBE_RegisterExecutionBackend"));
    auto load = reinterpret_cast<LoadXbe>(
        GetProcAddress(dll, "LoadXbe"));
    auto unload = reinterpret_cast<UnloadXbe>(
        GetProcAddress(dll, "UnloadXbe"));
    assert(start && stop && registerBackend && load && unload);
    assert(start() == 0);
    assert(load(path) == 4); // no backend must never be reported as a launch

    XBE_EXECUTION_BACKEND backend{};
    backend.size = sizeof(backend);
    backend.version = XBE_EXECUTION_BACKEND_VERSION;
    backend.reserveImage = ReserveImage;
    backend.releaseImage = ReleaseImage;
    backend.resolveImports = ResolveImports;
    backend.startGuest = StartGuest;
    backend.stopGuest = StopGuest;
    assert(registerBackend(&backend) == 0);

    assert(load(path) == 0);
    assert(g_reserved && g_importsResolved && g_guestStarted);
    assert(load(path) == 3); // reject a second image while one is active
    unload();
    unload(); // idempotent shutdown
    assert(g_guestStopped && g_released);
    assert(g_reserveOrder < g_resolveOrder &&
           g_resolveOrder < g_startOrder &&
           g_startOrder < g_stopOrder &&
           g_stopOrder < g_releaseOrder);

    stop();
    FreeLibrary(dll);
    DeleteFileW(path);
    std::puts("XBE loader backend handoff tests passed");
    return 0;
}
