#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "xdvdfs.h"

static void Put16(std::vector<uint8_t>& b, size_t o, uint16_t v) {
    b[o] = uint8_t(v); b[o + 1] = uint8_t(v >> 8);
}
static void Put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = uint8_t(v >> (i * 8));
}
static bool WriteImage(const wchar_t* path, const std::vector<uint8_t>& image) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    return out && bool(out.write(reinterpret_cast<const char*>(image.data()), image.size()));
}
static int Fail(const char* message) {
    std::fprintf(stderr, "XDVDFS smoke test failed: %s\n", message);
    return 1;
}

int main() {
    constexpr size_t sector = XDVDFS_SECTOR_SIZE;
    std::vector<uint8_t> image(64 * sector, 0xFF);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    const size_t descriptor = 32 * sector;
    std::memcpy(image.data() + descriptor, magic, 20);
    Put32(image, descriptor + 0x14, 40);
    Put32(image, descriptor + 0x18, 48);
    std::memcpy(image.data() + descriptor + 0x7EC, magic, 20);

    // Root directory: HELLO.TXT is the root node; MEDIA is its right child.
    const size_t root = 40 * sector;
    Put16(image, root, 0xFFFF);
    Put16(image, root + 2, 6); // child byte offset 24, expressed in DWORDs
    Put32(image, root + 4, 50);
    Put32(image, root + 8, 5);
    image[root + 12] = XDVDFS_ATTR_ARCHIVE;
    image[root + 13] = 9;
    std::memcpy(image.data() + root + 14, "HELLO.TXT", 9);

    const size_t media = root + 24;
    Put16(image, media, 0xFFFF);
    Put16(image, media + 2, 0xFFFF);
    Put32(image, media + 4, 42);
    Put32(image, media + 8, 24);
    image[media + 12] = XDVDFS_ATTR_DIRECTORY;
    image[media + 13] = 5;
    std::memcpy(image.data() + media + 14, "MEDIA", 5);

    // Nested directory containing A.TXT.
    const size_t nested = 42 * sector;
    Put16(image, nested, 0xFFFF);
    Put16(image, nested + 2, 0xFFFF);
    Put32(image, nested + 4, 51);
    Put32(image, nested + 8, 5);
    image[nested + 12] = XDVDFS_ATTR_ARCHIVE;
    image[nested + 13] = 5;
    std::memcpy(image.data() + nested + 14, "A.TXT", 5);
    std::memcpy(image.data() + 50 * sector, "hello", 5);
    std::memcpy(image.data() + 51 * sector, "world", 5);

    const wchar_t* path = L"xdvdfs-smoke.iso";
    if (!WriteImage(path, image)) return Fail("could not write synthetic image");
    if (ModuleStart() != 0) return Fail("ModuleStart failed");

    XDVDFS_VOLUME_INFO volume{};
    if (XDVDFS_OpenImage(path, &volume) != 0) return Fail("valid XISO image was rejected");
    if (volume.rootDirectorySector != 40 || volume.rootDirectorySize != 48)
        return Fail("root descriptor metadata mismatch");

    XDVDFS_FILE_INFO file{};
    if (XDVDFS_GetFileInfo("hello.txt", &file) != 0 ||
        file.size != 5 || file.startSector != 50 || file.nameLength != 9)
        return Fail("case-insensitive root lookup failed");
    if (XDVDFS_GetFileInfo("MEDIA/a.txt", &file) != 0 ||
        file.size != 5 || file.startSector != 51)
        return Fail("nested directory lookup failed");

    char data[8]{};
    uint32_t bytes = 0;
    if (XDVDFS_ReadFile("media\\A.TXT", 0, data, 5, &bytes) != 0 ||
        bytes != 5 || std::memcmp(data, "world", 5) != 0)
        return Fail("nested file read failed");
    if (XDVDFS_ReadFile("HELLO.TXT", 3, data, 5, &bytes) != 0 ||
        bytes != 2 || std::memcmp(data, "lo", 2) != 0)
        return Fail("bounded partial read failed");
    if (XDVDFS_ReadFile("MEDIA", 0, data, 1, &bytes) != 4 || bytes != 0)
        return Fail("directory read was not rejected");

    XDVDFS_CloseImage();

    // A binary directory tree cannot alias one node from both child links.
    Put16(image, root, 6);
    Put16(image, root + 2, 6);
    if (!WriteImage(path, image)) return Fail("could not write malformed image");
    if (XDVDFS_OpenImage(path, &volume) == 0)
        return Fail("aliased directory child pointers were accepted");

    ModuleStop();
    DeleteFileW(path);
    std::puts("PASS: XDVDFS descriptor, nested tree lookup, bounded reads, and malformed-tree rejection");
    return 0;
}
