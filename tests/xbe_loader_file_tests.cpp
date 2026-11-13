#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../modules/xbe_loader/xbe_backend_api.h"

#include <cstdint>
#include <cstdio>
#include <new>

struct TestBackendState {
    std::uint8_t* image = nullptr;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    bool resolved = false;
    bool started = false;
    bool stopped = false;
    bool released = false;
};

using ModuleStart = int (__cdecl*)();
using ModuleStop = void (__cdecl*)();
using RegisterBackendV2 = int (__cdecl*)(const XBE_EXECUTION_BACKEND_V2*);
using LoadXbe = int (__cdecl*)(const wchar_t*);
using UnloadXbe = void (__cdecl*)();

static int __cdecl ReserveImage(std::uint32_t base, std::uint32_t size,
                                std::uint8_t** image, void* opaque) {
    auto* state = static_cast<TestBackendState*>(opaque);
    if (!state || !image || size == 0 ||
        static_cast<std::uint64_t>(base) + size > 0x100000000ull)
        return 1;
    state->image = new (std::nothrow) std::uint8_t[size];
    if (!state->image) return 2;
    state->base = base;
    state->size = size;
    *image = state->image;
    return 0;
}

static void __cdecl ReleaseImage(std::uint8_t* image, void* opaque) {
    auto* state = static_cast<TestBackendState*>(opaque);
    if (!state) return;
    delete[] image;
    state->image = nullptr;
    state->released = true;
}

static int __cdecl ResolveImports(std::uint8_t* image,
                                  const XBE_IMAGE_INFO* info, void* opaque) {
    auto* state = static_cast<TestBackendState*>(opaque);
    if (!state || !image || image != state->image || !info ||
        info->baseAddress != state->base || info->imageSize != state->size ||
        info->sectionCount == 0 || info->entryPoint < info->baseAddress ||
        static_cast<std::uint64_t>(info->entryPoint) >=
            static_cast<std::uint64_t>(info->baseAddress) + info->imageSize)
        return 1;
    state->resolved = true;
    return 0;
}

static int __cdecl StartGuest(const XBE_IMAGE_INFO* info,
                              std::uint8_t* image, void* opaque) {
    auto* state = static_cast<TestBackendState*>(opaque);
    if (!state || !state->resolved || !info || !image ||
        image != state->image || info->entryPoint < info->baseAddress)
        return 1;
    const std::uint32_t entryOffset = info->entryPoint - info->baseAddress;
    if (entryOffset >= state->size) return 2;

    // This test validates the real file -> parser -> mapper -> backend handoff.
    // It deliberately does not execute Xbox machine code in the Windows host.
    state->started = true;
    std::printf("Mapped XBE: base=0x%08X size=0x%08X entry=0x%08X "
                "TLS=0x%08X sections=%u entry-byte=0x%02X\n",
                info->baseAddress, info->imageSize, info->entryPoint,
                info->tlsAddress, info->sectionCount, image[entryOffset]);
    return 0;
}

static void __cdecl StopGuest(void* opaque) {
    auto* state = static_cast<TestBackendState*>(opaque);
    if (state) state->stopped = true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: xbe_loader_file_tests.exe <sample.xbe>\n");
        return 2;
    }

    HMODULE dll = LoadLibraryW(L"xbe_loader.dll");
    if (!dll) {
        std::fprintf(stderr, "FAIL: LoadLibraryW(xbe_loader.dll), error=%lu\n",
                     GetLastError());
        return 3;
    }

    auto start = reinterpret_cast<ModuleStart>(
        GetProcAddress(dll, "ModuleStart"));
    auto stop = reinterpret_cast<ModuleStop>(
        GetProcAddress(dll, "ModuleStop"));
    auto registerBackend = reinterpret_cast<RegisterBackendV2>(
        GetProcAddress(dll, "XBE_RegisterExecutionBackendV2"));
    auto load = reinterpret_cast<LoadXbe>(
        GetProcAddress(dll, "LoadXbe"));
    auto unload = reinterpret_cast<UnloadXbe>(
        GetProcAddress(dll, "UnloadXbe"));

    if (!start || !stop || !registerBackend || !load || !unload) {
        std::fprintf(stderr, "FAIL: required xbe_loader export missing\n");
        FreeLibrary(dll);
        return 4;
    }

    TestBackendState state{};
    XBE_EXECUTION_BACKEND_V2 backend{};
    backend.size = sizeof(backend);
    backend.version = XBE_EXECUTION_BACKEND_V2_VERSION;
    backend.context = &state;
    backend.reserveImage = ReserveImage;
    backend.releaseImage = ReleaseImage;
    backend.resolveImports = ResolveImports;
    backend.startGuest = StartGuest;
    backend.stopGuest = StopGuest;

    int result = 0;
    if (start() != 0 || registerBackend(&backend) != 0) {
        std::fprintf(stderr, "FAIL: module/backend initialization\n");
        result = 5;
    } else if (load(argv[1]) != 0) {
        std::fprintf(stderr, "FAIL: LoadXbe rejected NXDK sample\n");
        result = 6;
    } else if (!state.resolved || !state.started) {
        std::fprintf(stderr, "FAIL: backend did not receive mapped image\n");
        result = 7;
    } else {
        unload();
        if (!state.stopped || !state.released) {
            std::fprintf(stderr, "FAIL: stop/release lifecycle incomplete\n");
            result = 8;
        }
    }

    stop();
    FreeLibrary(dll);
    if (result == 0)
        std::puts("PASS: NXDK-built XBE parsed, mapped, handed off, and released");
    return result;
}
