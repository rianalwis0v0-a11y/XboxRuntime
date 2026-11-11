
#pragma once

#include <windows.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

struct FATX_BOOT_SECTOR {
    uint32_t SectorSize;
    uint32_t SectorsPerCluster;
    uint32_t RootDirectoryCluster;
    uint32_t TotalClusters;
};

__declspec(dllexport)
int __cdecl ModuleStart();

__declspec(dllexport)
void __cdecl ModuleStop();

__declspec(dllexport)
int __cdecl FATX_ParseBootSector(
    const unsigned char* data,
    unsigned int size,
    FATX_BOOT_SECTOR* result
);

#ifdef __cplusplus
}
#endif
