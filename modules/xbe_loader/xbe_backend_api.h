#pragma once
#ifndef XBOX_RUNTIME_XBE_BACKEND_API_H
#define XBOX_RUNTIME_XBE_BACKEND_API_H

#include <cstdint>

#define XBE_EXECUTION_BACKEND_VERSION 1u

struct XBE_IMAGE_INFO {
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
};

// The kernel returns this table from XBE_GetExecutionBackend. guestImage is a
// contiguous host view of the guest VA range [guestBase, guestBase+imageSize).
// Callbacks return zero on success. The kernel owns guest memory and x86 CPU
// execution; XBE entry points are never called as native Windows functions.
struct XBE_EXECUTION_BACKEND {
    std::uint32_t size;
    std::uint32_t version;
    void* context;
    int (__cdecl* reserveImage)(std::uint32_t guestBase, std::uint32_t imageSize,
                                std::uint8_t** guestImage, void* context);
    void (__cdecl* releaseImage)(std::uint8_t* guestImage, void* context);
    int (__cdecl* resolveImports)(std::uint8_t* guestImage,
                                        const XBE_IMAGE_INFO* imageInfo,
                                        void* context);
    int (__cdecl* startGuest)(std::uint32_t entryPoint,
                              std::uint32_t tlsAddress, void* context);
    void (__cdecl* stopGuest)(void* context);
};


#define XBE_EXECUTION_BACKEND_V2_VERSION 2u

// ABI v2 keeps the v1 image metadata and memory/import callbacks, but gives
// startup the validated image description and the host-side mapped image
// view. guestImage is a host-process pointer only: a separate 32-bit guest
// process must map the same backing pages at imageInfo->baseAddress; it must
// never dereference this host pointer or treat entryPoint as a host pointer.
// reserveImage must reserve the exact requested guestBase or fail. Relocation
// is deliberately not part of ABI v2. imageInfo is valid only for the duration
// of startGuest; copy any metadata that must outlive the callback. guestImage
// remains owned by the backend until releaseImage is called after stopGuest.
struct XBE_EXECUTION_BACKEND_V2 {
    std::uint32_t size;
    std::uint32_t version;
    void* context;
    int (__cdecl* reserveImage)(std::uint32_t guestBase, std::uint32_t imageSize,
                                std::uint8_t** guestImage, void* context);
    void (__cdecl* releaseImage)(std::uint8_t* guestImage, void* context);
    int (__cdecl* resolveImports)(std::uint8_t* guestImage,
                                  const XBE_IMAGE_INFO* imageInfo,
                                  void* context);
    int (__cdecl* startGuest)(const XBE_IMAGE_INFO* imageInfo,
                              std::uint8_t* guestImage, void* context);
    void (__cdecl* stopGuest)(void* context);
};

// Implemented by xbox_kernel.dll. The host calls this after every module has
// started and before loading the dashboard XBE.
#ifdef __cplusplus
extern "C" {
#endif
int __cdecl XBE_GetExecutionBackend(XBE_EXECUTION_BACKEND* backend);
int __cdecl XBE_GetExecutionBackendV2(XBE_EXECUTION_BACKEND_V2* backend);
#ifdef __cplusplus
}
#endif

#endif
