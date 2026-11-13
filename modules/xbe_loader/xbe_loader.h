#pragma once
#ifndef XBOX_RUNTIME_XBE_LOADER_H
#define XBOX_RUNTIME_XBE_LOADER_H

#include <windows.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define XBE_EXECUTION_BACKEND_VERSION 1u

typedef struct XBE_IMAGE_INFO {
    std::uint32_t baseAddress;
    std::uint32_t imageSize;
    std::uint32_t headerSize;
    std::uint32_t certificateAddress;
    std::uint32_t nonKernelImportDirectoryAddress;
    std::uint32_t libraryVersionCount;
    std::uint32_t libraryVersionsAddress;
    std::uint32_t kernelLibraryVersionAddress;
    std::uint32_t xapiLibraryVersionAddress;
    std::uint32_t entryPoint;
    std::uint32_t tlsAddress;
    std::uint32_t kernelThunkAddress;
    std::uint32_t sectionCount;
    std::uint32_t debugImage;
} XBE_IMAGE_INFO;

// The runtime kernel supplies this backend. guestImage is a host pointer to a
// contiguous guest-VA image window; guest VAs are imageInfo.baseAddress + offset.
// Every callback returns zero on success. The loader never executes host code
// from an XBE or treats an XBE entry point as a native Windows function.
typedef struct XBE_EXECUTION_BACKEND {
    std::uint32_t size;
    std::uint32_t version;
    void* context;
    int (__cdecl* reserveImage)(std::uint32_t guestBase, std::uint32_t imageSize,
                                std::uint8_t** guestImage, void* context);
    void (__cdecl* releaseImage)(std::uint8_t* guestImage, void* context);
    int (__cdecl* resolveKernelImports)(std::uint8_t* guestImage,
                                        const XBE_IMAGE_INFO* imageInfo,
                                        void* context);
    int (__cdecl* startGuest)(std::uint32_t entryPoint,
                              std::uint32_t tlsAddress, void* context);
    void (__cdecl* stopGuest)(void* context);
} XBE_EXECUTION_BACKEND;

__declspec(dllexport) int __cdecl ModuleStart();
__declspec(dllexport) void __cdecl ModuleStop();
__declspec(dllexport) int __cdecl XBE_RegisterExecutionBackend(
    const XBE_EXECUTION_BACKEND* backend);
__declspec(dllexport) int __cdecl LoadXbe(const wchar_t* path);
__declspec(dllexport) void __cdecl UnloadXbe();

#ifdef __cplusplus
}
#endif
#endif
