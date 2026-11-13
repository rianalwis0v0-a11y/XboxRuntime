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
static bool WriteImage(const wchar_t* path, const std::vector<BYTE>& image) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, image.data(), DWORD(image.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == image.size();
}

int main() {
    constexpr DWORD imageSize = 64 * 1024;
    constexpr DWORD dataOffset = 0x2000;
    constexpr DWORD clusterSize = 1024;
    constexpr DWORD payloadSize = 1500;
    std::vector<BYTE> payload(payloadSize);
    for (DWORD i = 0; i < payloadSize; ++i) payload[i] = BYTE((i * 37u) % 251u);

    std::vector<BYTE> image(imageSize, 0);
    std::memcpy(image.data(), "FATX", 4);
    Put32(image, 8, 2);       // sectors per cluster
    Put32(image, 12, 1);      // root directory cluster
    Put16(image, 0x1000 + 2, 0xFFF8); // cluster 1: root directory end
    Put16(image, 0x1000 + 4, 3);      // cluster 2 -> cluster 3
    Put16(image, 0x1000 + 6, 0xFFF8); // cluster 3: end of chain

    BYTE* root = image.data() + dataOffset;
    root[0] = 8;
    root[1] = FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(root + 2, "TEST.BIN", 8);
    Put32(image, dataOffset + 0x2C, 2);
    Put32(image, dataOffset + 0x30, payloadSize);
    root[64] = 0xFF;
    std::memcpy(image.data() + dataOffset + clusterSize, payload.data(), clusterSize);
    std::memcpy(image.data() + dataOffset + clusterSize * 2, payload.data() + clusterSize,
                payloadSize - clusterSize);

    wchar_t tempPath[MAX_PATH]{}, filePath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath) ||
        !GetTempFileNameW(tempPath, L"fat", 0, filePath))
        return Fail("could not create temporary image path");
    if (!WriteImage(filePath, image)) {
        DeleteFileW(filePath);
        return Fail("could not write synthetic image");
    }

    int result = 1;
    FATX_VOLUME volume = nullptr;
    if (ModuleStart() != FATX_OK) {
        Fail("ModuleStart failed");
        goto cleanup;
    }
    if (FATX_OpenVolumeW(filePath, imageSize - 512, 1024, &volume) != FATX_INVALID_ARGUMENT) {
        Fail("out-of-range partition was accepted");
        goto stop;
    }
    if (FATX_OpenVolumeW(filePath, 0, imageSize, &volume) != FATX_OK) {
        Fail("could not open synthetic FATX volume");
        goto stop;
    }
    {
        FATX_INFO info{};
        if (FATX_GetInfo(volume, &info) != FATX_OK ||
            info.sectorSize != 512 || info.sectorsPerCluster != 2 ||
            info.clusterSize != clusterSize || info.rootDirectoryCluster != 1 ||
            info.fatEntrySize != 2) {
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
            std::wcscmp(entry.name, L"TEST.BIN") != 0 || entry.size != payloadSize) {
            Fail("root directory listing failed");
            goto stop;
        }
    }
    {
        std::vector<BYTE> actual(payloadSize);
        DWORD bytesRead = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 0, actual.data(), payloadSize, &bytesRead) != FATX_OK ||
            bytesRead != payloadSize || actual != payload) {
            Fail("multi-cluster file read failed");
            goto stop;
        }
        BYTE tail[500]{};
        if (FATX_ReadFileW(volume, L"TEST.BIN", 1000, tail, sizeof(tail), &bytesRead) != FATX_OK ||
            bytesRead != 500 || std::memcmp(tail, payload.data() + 1000, 500) != 0) {
            Fail("cross-cluster offset read failed");
            goto stop;
        }
    }
    FATX_CloseVolume(volume);
    volume = nullptr;

    // Reopen a cyclic FAT chain and verify that reads stop with corruption,
    // rather than looping or returning fabricated data.
    Put16(image, 0x1000 + 4, 2);
    if (!WriteImage(filePath, image)) {
        Fail("could not write cyclic FATX image");
        goto stop;
    }
    if (FATX_OpenVolumeW(filePath, 0, imageSize, &volume) != FATX_OK) {
        Fail("could not reopen cyclic FATX image");
        goto stop;
    }
    {
        BYTE buffer[payloadSize]{};
        DWORD bytesRead = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 0, buffer, payloadSize, &bytesRead) != FATX_CORRUPT ||
            bytesRead != clusterSize) {
            Fail("cyclic FAT chain was not detected");
            goto stop;
        }
    }
    std::puts("PASS: FATX mount, metadata, directory listing, multi-cluster reads, bounds, and cycle detection");
    result = 0;
stop:
    if (volume) FATX_CloseVolume(volume);
    ModuleStop();
cleanup:
    DeleteFileW(filePath);
    return result;
}
