#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "xdvdfs.h"

static void Put16(std::vector<uint8_t>& b, size_t o, uint16_t v) { b[o]=uint8_t(v); b[o+1]=uint8_t(v>>8); }
static void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) { for (int i=0;i<4;++i) b[o+i]=uint8_t(v>>(i*8)); }

int main() {
    constexpr size_t sector = 2048;
    std::vector<uint8_t> image(64 * sector, 0xFF);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(image.data()+32*sector, magic, 20);
    Put32(image, 32*sector+0x14, 40);
    Put32(image, 32*sector+0x18, 20);
    std::memcpy(image.data()+32*sector+0x7EC, magic, 20);

    const size_t entry = 40*sector;
    Put16(image, entry, 0xFFFF);
    Put16(image, entry+2, 0xFFFF);
    Put32(image, entry+4, 50);
    Put32(image, entry+8, 5);
    image[entry+12] = XDVDFS_ATTR_ARCHIVE;
    image[entry+13] = 8;
    std::memcpy(image.data()+entry+14, "TEST.TXT", 8);
    std::memcpy(image.data()+50*sector, "hello", 5);

    const wchar_t* path = L"xdvdfs-smoke.iso";
    { std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(image.data()), image.size()); }
    if (ModuleStart() != 0) return 1;
    XDVDFS_VOLUME_INFO volume{};
    if (XDVDFS_OpenImage(path, &volume) != 0) return 2;
    if (volume.rootDirectorySector != 40 || volume.rootDirectorySize != 20) return 3;
    XDVDFS_FILE_INFO file{};
    if (XDVDFS_GetFileInfo("test.txt", &file) != 0) return 4;
    if (file.size != 5 || file.startSector != 50 || file.nameLength != 8) return 5;
    char data[6]{};
    uint32_t bytes = 0;
    if (XDVDFS_ReadFile("TEST.TXT", 0, data, 5, &bytes) != 0) return 6;
    if (bytes != 5 || std::memcmp(data, "hello", 5) != 0) return 7;
    if (XDVDFS_ReadFile("TEST.TXT", 3, data, 5, &bytes) != 0 || bytes != 2 || std::memcmp(data, "lo", 2) != 0) return 8;
    XDVDFS_CloseImage();
    ModuleStop();
    DeleteFileW(path);
    std::puts("PASS: XDVDFS descriptor, directory tree, case-insensitive lookup, and bounded file reads");
    return 0;
}
