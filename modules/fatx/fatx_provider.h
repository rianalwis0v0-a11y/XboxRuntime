#pragma once
#include "guest32/kernel_filesystem_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) LONG __stdcall FATX_RegisterFilesystemProvider(
    const WCHAR* deviceName, const WCHAR* backingDevicePath,
    ULONGLONG partitionOffset, ULONGLONG partitionLength,
    XBOX_FS_MOUNT* mountHandle);
__declspec(dllexport) LONG __stdcall FATX_UnregisterFilesystemProvider(XBOX_FS_MOUNT mountHandle);
#ifdef __cplusplus
}
#endif
