
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "fatx.h"

static bool g_initialized = false;

extern "C" int __cdecl ModuleStart()
{
    if (g_initialized)
        return 0;

    g_initialized = true;

    OutputDebugStringW(
        L"[XboxRuntime:FATX] FATX module initialized\n"
    );

    return 0;
}

extern "C" void __cdecl ModuleStop()
{
    if (!g_initialized)
        return;

    g_initialized = false;

    OutputDebugStringW(
        L"[XboxRuntime:FATX] FATX module stopped\n"
    );
}

extern "C" int __cdecl FATX_ParseBootSector(
    const unsigned char* data,
    unsigned int size,
    FATX_BOOT_SECTOR* result
)
{
    if (!g_initialized || !data || !result || size < 16)
        return 1;

    // FATX boot-sector fields are little-endian.
    auto read32 = [](const unsigned char* p) -> uint32_t {
        return
            (uint32_t)p[0] |
            ((uint32_t)p[1] << 8) |
            ((uint32_t)p[2] << 16) |
            ((uint32_t)p[3] << 24);
    };

    result->SectorSize = 512;
    result->SectorsPerCluster = data[0];
    result->RootDirectoryCluster = read32(data + 4);
    result->TotalClusters = read32(data + 8);

    if (result->SectorsPerCluster == 0 ||
        result->TotalClusters == 0)
        return 2;

    return 0;
}

BOOL WINAPI DllMain(
    HINSTANCE instance,
    DWORD reason,
    LPVOID reserved
)
{
    return TRUE;
}
