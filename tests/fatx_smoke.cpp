#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cwchar>
#include "fatx.h"

static void Put32(std::vector<BYTE>& b, size_t offset, uint32_t value) {
    b[offset] = BYTE(value);
    b[offset + 1] = BYTE(value >> 8);
    b[offset + 2] = BYTE(value >> 16);
    b[offset + 3] = BYTE(value >> 24);
}
static void Put16(std::vector<BYTE>& b, size_t offset, uint16_t value) {
    b[offset] = BYTE(value);
    b[offset + 1] = BYTE(value >> 8);
}
static int Fail(const char* message) {
    std::fprintf(stderr, "FATX smoke test failed: %s\n", message);
    return 1;
}
int main() {
    constexpr DWORD imageSize = 64 * 1024;
    constexpr DWORD dataOffset = 0x2000;
    constexpr DWORD clusterSize = 1024;
    const char payload[] = "XboxRuntime FATX read test";
    std::vector<BYTE> image(imageSize, 0);

    std::memcpy(image.data(), "FATX", 4);
    Put32(image, 8, 2);       // sectors per cluster
    Put32(image, 12, 1);     // root directory cluster
    Put16(image, 0x1000 + 2, 0xFFF8); // cluster 1: end of chain
    Put16(image, 0x1000 + 4, 0xFFF8); // cluster 2: end of chain

    BYTE* root = image.data() + dataOffset;
    root[0] = 8;
    root[1] = FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(root + 2, "TEST.TXT", 8);
    Put32(image, dataOffset + 0x2C, 2);
    Put32(image, dataOffset + 0x30, DWORD(sizeof(payload) - 1));
    root[64] = 0xFF;
    std::memcpy(image.data() + dataOffset + clusterSize, payload, sizeof(payload) - 1);

    wchar_t tempPath[MAX_PATH]{}, filePath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath) ||
        !GetTempFileNameW(tempPath, L"fat", 0, filePath))
        return Fail("could not create temporary image path");
    HANDLE file = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(filePath);
        return Fail("could not create image file");
    }
    DWORD written = 0;
    BOOL writeOk = WriteFile(file, image.data(), imageSize, &written, nullptr);
    CloseHandle(file);
    if (!writeOk || written != imageSize) {
        DeleteFileW(filePath);
        return Fail("could not write synthetic image");
    }

    int result = 1;
    FATX_VOLUME volume = nullptr;
    if (ModuleStart() != FATX_OK) {
        Fail("ModuleStart failed");
        goto cleanup;
    }
    if (FATX_OpenVolumeW(filePath, 0, imageSize, &volume) != FATX_OK) {
        Fail("could not open synthetic FATX volume");
        goto stop;
    }
    {
        FATX_INFO info{};
        if (FATX_GetInfo(volume, &info) != FATX_OK ||
            info.sectorsPerCluster != 2 ||
            info.clusterSize != clusterSize ||
            info.rootDirectoryCluster != 1) {
            Fail("volume metadata mismatch");
            goto stop;
        }
    }
    {
        DWORD required = 0;
        if (FATX_ListDirectoryW(volume, L"\\", nullptr, 0, &required) != FATX_BUFFER_TOO_SMALL ||
            required != 1) {
            Fail("root directory sizing failed");
            goto stop;
        }
        FATX_ENTRY entry{};
        if (FATX_ListDirectoryW(volume, L"\\", &entry, 1, &required) != FATX_OK ||
            std::wcscmp(entry.name, L"TEST.TXT") != 0 ||
            entry.size != sizeof(payload) - 1) {
            Fail("root directory listing failed");
            goto stop;
        }
    }
    {
        char buffer[64]{};
        DWORD bytesRead = 0;
        if (FATX_ReadFileW(volume, L"TEST.TXT", 0, buffer, sizeof(buffer), &bytesRead) != FATX_OK ||
            bytesRead != sizeof(payload) - 1 ||
            std::memcmp(buffer, payload, bytesRead) != 0) {
            Fail("file data read failed");
            goto stop;
        }
    }
    std::puts("PASS: FATX mount, metadata, directory listing, and file read.");
    result = 0;
stop:
    if (volume) FATX_CloseVolume(volume);
    ModuleStop();
cleanup:
    DeleteFileW(filePath);
    return result;
}
