#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

// Configure image-backed volumes for the guest process. The environment
// values are inherited by xbe_guest32 when xbox_kernel launches it.
__declspec(dllexport) int __cdecl XboxStorageSetFatxImage(
    const wchar_t* imagePath, const wchar_t* deviceName,
    ULONGLONG partitionOffset, ULONGLONG partitionLength);
__declspec(dllexport) int __cdecl XboxStorageSetXdvdfsImage(
    const wchar_t* imagePath, const wchar_t* deviceName);
__declspec(dllexport) void __cdecl XboxStorageClearConfiguration(void);

__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);
#ifdef __cplusplus
}
#endif
