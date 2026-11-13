#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../modules/xbe_loader/xbe_backend_api.h"

namespace {
using ModuleStartFn = int (__cdecl*)();
using ModuleStopFn = void (__cdecl*)();
using GetBackendFn = int (__cdecl*)(XBE_EXECUTION_BACKEND_V2*);

#pragma pack(push, 1)
struct TestSection {
    std::uint32_t flags;
    std::uint32_t virtualAddress;
    std::uint32_t virtualSize;
    std::uint32_t rawAddress;
    std::uint32_t rawSize;
    std::uint32_t nameAddress;
    std::uint32_t nameReferenceCount;
    std::uint32_t headSharedPageReferenceCountAddress;
    std::uint32_t tailSharedPageReferenceCountAddress;
    std::uint8_t digest[20];
};
#pragma pack(pop)
static_assert(sizeof(TestSection) == 56, "XBE section header layout");

int Fail(const char* message, int code, XBE_EXECUTION_BACKEND_V2* backend,
         std::uint8_t* image, ModuleStopFn stop) {
    std::fprintf(stderr, "kernel_guest_backend_smoke: %s (%d)\n", message, code);
    if (backend && backend->stopGuest) backend->stopGuest(backend->context);
    if (image && backend && backend->releaseImage)
        backend->releaseImage(image, backend->context);
    if (stop) stop();
    return code ? code : 1;
}
}

int main() {
    HMODULE module = LoadLibraryW(L"xbox_kernel.dll");
    if (!module) {
        std::fprintf(stderr, "LoadLibrary xbox_kernel.dll failed: %lu\n", GetLastError());
        return 1;
    }
    auto start = reinterpret_cast<ModuleStartFn>(
        GetProcAddress(module, "ModuleStart"));
    auto stop = reinterpret_cast<ModuleStopFn>(
        GetProcAddress(module, "ModuleStop"));
    auto getBackend = reinterpret_cast<GetBackendFn>(
        GetProcAddress(module, "XBE_GetExecutionBackendV2"));
    if (!start || !stop || !getBackend || start() != 0) {
        std::fprintf(stderr, "kernel module exports/startup failed\n");
        FreeLibrary(module);
        return 2;
    }

    XBE_EXECUTION_BACKEND_V2 backend{};
    backend.size = sizeof(backend);
    backend.version = XBE_EXECUTION_BACKEND_V2_VERSION;
    if (getBackend(&backend) != 0 ||
        backend.version != XBE_EXECUTION_BACKEND_V2_VERSION ||
        !backend.reserveImage || !backend.resolveImports ||
        !backend.startGuest || !backend.stopGuest || !backend.releaseImage) {
        stop();
        FreeLibrary(module);
        return 3;
    }

    constexpr std::uint32_t guestBase = 0x10000000u;
    constexpr std::uint32_t imageSize = 0x4000u;
    constexpr std::uint32_t headerSize = 0x1000u;
    constexpr std::uint32_t sectionTable = guestBase + 0x400u;
    constexpr std::uint32_t entryPoint = guestBase + 0x2000u;
    std::uint8_t* image = nullptr;
    const int reserve = backend.reserveImage(
        guestBase, imageSize, &image, backend.context);
    if (reserve != 0 || !image)
        return Fail("exact-base shared image reservation failed", reserve, &backend, image, stop);

    std::memset(image, 0, imageSize);
    std::memcpy(image + 0x120, &sectionTable, sizeof(sectionTable));
    const TestSection section{
        0x04u, entryPoint, 0x1000u, 0u, 0u, 0u, 0u, 0u, 0u, {}
    };
    std::memcpy(image + 0x400, &section, sizeof(section));
    image[0x2000] = 0xC3; // Minimal x86 guest entry: ret.

    XBE_IMAGE_INFO info{};
    info.baseAddress = guestBase;
    info.imageSize = imageSize;
    info.headerSize = headerSize;
    info.entryPoint = entryPoint;
    info.sectionCount = 1;
    info.kernelThunkAddress = 0; // No kernel imports in this minimal image.
    info.nonKernelImportDirectoryAddress = 0;

    const int resolve = backend.resolveImports(image, &info, backend.context);
    if (resolve != 0)
        return Fail("guest import setup failed", resolve, &backend, image, stop);

    const int execute = backend.startGuest(&info, image, backend.context);
    if (execute != 0)
        return Fail("32-bit guest entry did not start", execute, &backend, image, stop);

    backend.stopGuest(backend.context);
    backend.releaseImage(image, backend.context);
    stop();
    FreeLibrary(module);
    std::puts("kernel_guest_backend_smoke: PASS (shared mapping + x86 entry + stop/release)");
    return 0;
}
