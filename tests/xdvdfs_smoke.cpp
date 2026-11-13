#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "xdvdfs.h"

static void Put16(std::vector<uint8_t>& b, size_t o, uint16_t v) {
    b[o] = uint8_t(v);
    b[o + 1] = uint8_t(v >> 8);
}
static void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = uint8_t(v >> (i * 8));
}
static void PutEntry(std::vector<uint8_t>& image, size_t offset,
    uint16_t left, uint16_t right, uint32_t sector, uint32_t size,
    uint8_t attributes, const char* name, uint8_t nameLength) {
    Put16(image, offset, left);
    Put16(image, offset + 2, right);
    Put32(image, offset + 4, sector);
    Put32(image, offset + 8, size);
    image[offset + 12] = attributes;
    image[offset + 13] = nameLength;
    std::memcpy(image.data() + offset + 14, name, nameLength);
}

int main() {
    constexpr size_t sector = 2048;
    std::vector<uint8_t> image(64 * sector, 0xFF);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(image.data() + 32 * sector, magic, 20);
    Put32(image, 32 * sector + 0x14, 40);
    Put32(image, 32 * sector + 0x18, 48);
    std::memcpy(image.data() + 32 * sector + 0x7EC, magic, 20);

    const size_t root = 40 * sector;
    PutEntry(image, root, 0xFFFF, 6, 50, 5,
        XDVDFS_ATTR_ARCHIVE, "TEST.TXT", 8);
    PutEntry(image, root + 24, 0xFFFF, 0xFFFF, 41, 24,
        XDVDFS_ATTR_DIRECTORY, "FOLDER", 6);
    PutEntry(image, 41 * sector, 0xFFFF, 0xFFFF, 51, 3,
        XDVDFS_ATTR_ARCHIVE, "INNER.TXT", 9);
    std::memcpy(image.data() + 50 * sector, "hello", 5);
    std::memcpy(image.data() + 51 * sector, "abc", 3);

    const wchar_t* path = L"xdvdfs-smoke.iso";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(image.data()), image.size());
    }

    if (ModuleStart() != 0) return 1;
    XDVDFS_VOLUME_INFO volume{};
    if (XDVDFS_OpenImage(path, &volume) != 0) return 2;
    if (volume.rootDirectorySector != 40 || volume.rootDirectorySize != 48) return 3;

    XDVDFS_FILE_INFO file{};
    if (XDVDFS_GetFileInfo("test.txt", &file) != 0) return 4;
    if (file.size != 5 || file.startSector != 50 || file.nameLength != 8) return 5;
    if (XDVDFS_GetFileInfo("folder/inner.txt", &file) != 0) return 6;
    if (file.size != 3 || file.startSector != 51) return 7;

    uint32_t required = 0;
    XDVDFS_FILE_INFO entries[2]{};
    if (XDVDFS_ListDirectory("", entries, 1, &required) != 7 || required != 2) return 8;
    if (XDVDFS_ListDirectory("", entries, 2, &required) != 0 || required != 2) return 9;
    if (std::strcmp(entries[0].name, "FOLDER") != 0 ||
        std::strcmp(entries[1].name, "TEST.TXT") != 0) return 10;
    if (!(entries[0].attributes & XDVDFS_ATTR_DIRECTORY)) return 11;

    if (XDVDFS_ListDirectory("folder", entries, 2, &required) != 0 ||
        required != 1 || std::strcmp(entries[0].name, "INNER.TXT") != 0) return 12;
    if (XDVDFS_ListDirectory("test.txt", entries, 2, &required) != 4) return 13;

    char data[8]{};
    uint32_t bytes = 0;
    if (XDVDFS_ReadFile("TEST.TXT", 0, data, 5, &bytes) != 0) return 14;
    if (bytes != 5 || std::memcmp(data, "hello", 5) != 0) return 15;
    if (XDVDFS_ReadFile("TEST.TXT", 3, data, 5, &bytes) != 0 ||
        bytes != 2 || std::memcmp(data, "lo", 2) != 0) return 16;
    if (XDVDFS_ReadFile("folder/inner.txt", 0, data, 3, &bytes) != 0 ||
        bytes != 3 || std::memcmp(data, "abc", 3) != 0) return 17;

    XDVDFS_CloseImage();
    ModuleStop();
    DeleteFileW(path);
    std::puts("PASS: XDVDFS descriptor, nested paths, bounded listing, case-insensitive lookup, and reads");
    return 0;
}
