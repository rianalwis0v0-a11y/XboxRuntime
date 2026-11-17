#pragma once
// Stable kernel-facing filesystem provider contract.
// FATX and XDVDFS modules implement this contract; the kernel bridge owns
// mount routing and forwards file operations without knowing on-disk formats.
#include <windows.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define XBOX_FS_PROVIDER_ABI_V1 1u
#define XBOX_FS_MOUNT_READ_ONLY 0x00000001u

typedef void* XBOX_FS_MOUNT;
typedef void* XBOX_FS_FILE;

typedef struct XBOX_FS_FILE_INFO {
    ULONGLONG size;
    ULONGLONG allocationSize;
    DWORD attributes;
    DWORD reserved;
} XBOX_FS_FILE_INFO;

// Provider paths are relative to the mounted volume, use backslashes, and
// are UTF-16. Providers must validate all ranges and return NTSTATUS values.
typedef struct XBOX_FS_PROVIDER_V1 {
    DWORD abiVersion;
    DWORD structureSize;
    const WCHAR* filesystemName;
    LONG (__stdcall* Open)(void* providerContext, const WCHAR* path,
        ACCESS_MASK desiredAccess, ULONG shareAccess, void** fileContext);
    LONG (__stdcall* Read)(void* providerContext, void* fileContext,
        ULONGLONG offset, void* buffer, ULONG length, ULONG* transferred);
    LONG (__stdcall* Write)(void* providerContext, void* fileContext,
        ULONGLONG offset, const void* buffer, ULONG length, ULONG* transferred);
    LONG (__stdcall* QueryInformation)(void* providerContext, void* fileContext,
        XBOX_FS_FILE_INFO* information);
    void (__stdcall* Close)(void* providerContext, void* fileContext);
} XBOX_FS_PROVIDER_V1;

// Mount names are absolute NT-style device paths (for example,
// \\Device\\Harddisk0\\Partition1 or \\Device\\CdRom0).
// Longest-prefix routing is used; a path separator must follow the mount name.
LONG __stdcall XboxFsMount(const WCHAR* deviceName,
    const XBOX_FS_PROVIDER_V1* provider, void* providerContext,
    DWORD mountFlags, XBOX_FS_MOUNT* mountHandle);
LONG __stdcall XboxFsUnmount(XBOX_FS_MOUNT mountHandle);
LONG __stdcall XboxFsOpen(const WCHAR* absolutePath, ACCESS_MASK desiredAccess,
    ULONG shareAccess, XBOX_FS_FILE* fileHandle);
LONG __stdcall XboxFsRead(XBOX_FS_FILE fileHandle, ULONGLONG offset,
    void* buffer, ULONG length, ULONG* transferred);
LONG __stdcall XboxFsWrite(XBOX_FS_FILE fileHandle, ULONGLONG offset,
    const void* buffer, ULONG length, ULONG* transferred);
LONG __stdcall XboxFsQueryInformation(XBOX_FS_FILE fileHandle,
    XBOX_FS_FILE_INFO* information);
void __stdcall XboxFsClose(XBOX_FS_FILE fileHandle);

#ifdef __cplusplus
}
#endif
