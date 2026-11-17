#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "xdvdfs.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: xdvdfs_real_xiso.exe <nxdk-xiso>\n");
        return 2;
    }
    if (ModuleStart() != 0) {
        std::fprintf(stderr, "FAIL: ModuleStart\n");
        return 1;
    }
    XDVDFS_VOLUME_INFO volume{};
    if (XDVDFS_OpenImage(argv[1], &volume) != 0) {
        ModuleStop();
        std::fprintf(stderr, "FAIL: NXDK-generated XISO rejected\n");
        return 1;
    }
    XDVDFS_FILE_INFO file{};
    if (XDVDFS_GetFileInfo("default.xbe", &file) != 0 ||
        file.size < 4 || (file.attributes & XDVDFS_ATTR_DIRECTORY) != 0) {
        XDVDFS_CloseImage();
        ModuleStop();
        std::fprintf(stderr, "FAIL: default.xbe missing or invalid\n");
        return 1;
    }
    char magic[4]{};
    uint32_t bytes = 0;
    const int readResult = XDVDFS_ReadFile("default.xbe", 0, magic, sizeof(magic), &bytes);
    XDVDFS_CloseImage();
    ModuleStop();
    if (readResult != 0 || bytes != 4 || std::memcmp(magic, "XBEH", 4) != 0) {
        std::fprintf(stderr, "FAIL: XBE header could not be read from real XISO\n");
        return 1;
    }
    std::printf("PASS: NXDK XISO parsed; default.xbe size=%lu; XBEH header verified\n",
                static_cast<unsigned long>(file.size));
    return 0;
}
