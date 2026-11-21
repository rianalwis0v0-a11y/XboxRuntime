#pragma once
#include "guest32/kernel_filesystem_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) LONG __stdcall XDVDFS_RegisterFilesystemProvider(
    const WCHAR* deviceName, const WCHAR* imagePath, XBOX_FS_MOUNT* mountHandle);
__declspec(dllexport) LONG __stdcall XDVDFS_UnregisterFilesystemProvider(XBOX_FS_MOUNT mountHandle);
#ifdef __cplusplus
}
#endif
