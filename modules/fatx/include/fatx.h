
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FATX_OK                 0
#define FATX_ERROR             -1
#define FATX_INVALID_ARGUMENT  -2
#define FATX_INVALID_VOLUME    -3
#define FATX_IO_ERROR          -4
#define FATX_NOT_FOUND         -5
#define FATX_NOT_DIRECTORY     -6
#define FATX_IS_DIRECTORY      -7
#define FATX_BUFFER_TOO_SMALL -8
#define FATX_CORRUPT           -9

#define FATX_ATTRIBUTE_READONLY 0x01
#define FATX_ATTRIBUTE_SYSTEM   0x02
#define FATX_ATTRIBUTE_HIDDEN   0x04
#define FATX_ATTRIBUTE_VOLUME   0x08
#define FATX_ATTRIBUTE_DIRECTORY 0x10
#define FATX_ATTRIBUTE_ARCHIVE  0x20

typedef void* FATX_VOLUME;

typedef struct FATX_INFO {
    DWORD sectorSize;
    DWORD sectorsPerCluster;
    DWORD clusterSize;
    DWORD fatEntrySize;
    DWORD rootDirectoryCluster;
    ULONGLONG partitionSize;
    ULONGLONG dataOffset;
    ULONGLONG clusterCount;
} FATX_INFO;

typedef struct FATX_ENTRY {
    WCHAR name[43];
    DWORD attributes;
    DWORD firstCluster;
    ULONGLONG size;
} FATX_ENTRY;

__declspec(dllexport) int __cdecl ModuleStart();
__declspec(dllexport) void __cdecl ModuleStop();

__declspec(dllexport)
int __cdecl FATX_OpenVolumeW(
    LPCWSTR devicePath,
    ULONGLONG partitionOffset,
    ULONGLONG partitionLength,
    FATX_VOLUME* volume
);

__declspec(dllexport)
int __cdecl FATX_OpenImageReadWriteW(
    LPCWSTR imagePath,
    ULONGLONG partitionOffset,
    ULONGLONG partitionLength,
    FATX_VOLUME* volume
);

__declspec(dllexport)
int __cdecl FATX_WriteFileW(
    FATX_VOLUME volume,
    LPCWSTR path,
    ULONGLONG offset,
    const void* buffer,
    DWORD bytesToWrite,
    DWORD* bytesWritten
);

__declspec(dllexport)
void __cdecl FATX_CloseVolume(FATX_VOLUME volume);

__declspec(dllexport)
int __cdecl FATX_GetInfo(
    FATX_VOLUME volume,
    FATX_INFO* info
);

__declspec(dllexport)
int __cdecl FATX_ListDirectoryW(
    FATX_VOLUME volume,
    LPCWSTR path,
    FATX_ENTRY* entries,
    DWORD capacity,
    DWORD* required
);

__declspec(dllexport)
int __cdecl FATX_ReadFileW(
    FATX_VOLUME volume,
    LPCWSTR path,
    ULONGLONG offset,
    void* buffer,
    DWORD bytesToRead,
    DWORD* bytesRead
);

#ifdef __cplusplus
}
#endif
