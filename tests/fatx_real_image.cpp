#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "fatx.h"

static void put16(std::vector<BYTE>& b, size_t o, uint16_t v) {
    b[o] = BYTE(v); b[o + 1] = BYTE(v >> 8);
}
static void put32(std::vector<BYTE>& b, size_t o, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[o + i] = BYTE(v >> (i * 8));
}
static bool writeAll(const wchar_t* path, const std::vector<BYTE>& bytes) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD done = 0;
    const BOOL ok = WriteFile(h, bytes.data(), DWORD(bytes.size()), &done, nullptr);
    CloseHandle(h);
    return ok && done == bytes.size();
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: fatx_real_image.exe <output.img>\n");
        return 2;
    }
    constexpr size_t imageSize = 64 * 1024, dataOffset = 0x2000, clusterSize = 1024;
    constexpr DWORD payloadSize = 1500;
    std::vector<BYTE> image(imageSize, 0), expected(payloadSize);
    std::memcpy(image.data(), "FATX", 4);
    put32(image, 8, 2);  // two 512-byte sectors per cluster
    put32(image, 12, 1); // root directory is cluster 1
    put16(image, 0x1000 + 2, 0xFFFF); // root EOC
    put16(image, 0x1000 + 4, 3);     // file cluster 2 -> 3
    put16(image, 0x1000 + 6, 0xFFF8);// file EOC
    BYTE* root = image.data() + dataOffset;
    root[0] = 8;
    root[1] = FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(root + 2, "TEST.BIN", 8);
    put32(image, dataOffset + 0x2C, 2);
    put32(image, dataOffset + 0x30, payloadSize);
    root[64] = 0xFF;
    for (DWORD i = 0; i < payloadSize; ++i) expected[i] = BYTE((i * 37u) % 251u);
    std::memcpy(image.data() + dataOffset + clusterSize, expected.data(), clusterSize);
    std::memcpy(image.data() + dataOffset + clusterSize * 2,
                expected.data() + clusterSize, payloadSize - clusterSize);
    if (!writeAll(argv[1], image)) {
        std::fprintf(stderr, "FAIL: could not write FATX image\n");
        return 1;
    }
    if (ModuleStart() != FATX_OK) return 1;
    FATX_VOLUME volume = nullptr;
    if (FATX_OpenVolumeW(argv[1], 0, imageSize, &volume) != FATX_OK) {
        ModuleStop();
        std::fprintf(stderr, "FAIL: generated FATX image rejected\n");
        return 1;
    }
    std::vector<BYTE> actual(payloadSize);
    DWORD bytesRead = 0;
    const int readStatus = FATX_ReadFileW(volume, L"\\TEST.BIN", 0,
                                          actual.data(), payloadSize, &bytesRead);
    FATX_CloseVolume(volume);
    ModuleStop();
    if (readStatus != FATX_OK || bytesRead != payloadSize || actual != expected) {
        std::fprintf(stderr, "FAIL: FATX multi-cluster data verification failed\n");
        return 1;
    }
    std::printf("PASS: generated FATX disk image; %lu-byte file read across two clusters\n",
                static_cast<unsigned long>(bytesRead));
    return 0;
}
