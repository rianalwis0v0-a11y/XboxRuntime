#pragma once
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XDVDFS_SECTOR_SIZE 2048u
#define XDVDFS_ATTR_READONLY 0x01u
#define XDVDFS_ATTR_HIDDEN   0x02u
#define XDVDFS_ATTR_SYSTEM   0x04u
#define XDVDFS_ATTR_DIRECTORY 0x10u
#define XDVDFS_ATTR_ARCHIVE  0x20u
#define XDVDFS_ATTR_NORMAL   0x80u

typedef struct XDVDFS_VOLUME_INFO {
    uint64_t partitionOffset;
    uint32_t rootDirectorySector;
    uint32_t rootDirectorySize;
    uint64_t imageSize;
} XDVDFS_VOLUME_INFO;

typedef struct XDVDFS_FILE_INFO {
    uint32_t startSector;
    uint32_t size;
    uint8_t attributes;
    uint8_t nameLength;
    char name[256];
} XDVDFS_FILE_INFO;

__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);
__declspec(dllexport) int __cdecl XDVDFS_OpenImage(const wchar_t* imagePath, XDVDFS_VOLUME_INFO* info);
__declspec(dllexport) void __cdecl XDVDFS_CloseImage(void);
__declspec(dllexport) int __cdecl XDVDFS_GetFileInfo(const char* path, XDVDFS_FILE_INFO* info);
__declspec(dllexport) int __cdecl XDVDFS_ReadFile(const char* path, uint64_t offset, void* buffer, uint32_t bytesToRead, uint32_t* bytesRead);

#ifdef __cplusplus
}
#endif
