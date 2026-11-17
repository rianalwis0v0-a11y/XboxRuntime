#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cwchar>
#include "fatx.h"

static void Put16(std::vector<BYTE>& b, size_t o, uint16_t v) {
    b[o] = BYTE(v); b[o + 1] = BYTE(v >> 8);
}
static void Put32(std::vector<BYTE>& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[o + i] = BYTE(v >> (8 * i));
}
static bool WriteImage(const wchar_t* path, const std::vector<BYTE>& image) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, image.data(), static_cast<DWORD>(image.size()), &written, nullptr);
    CloseHandle(f);
    return ok && written == image.size();
}
static bool WriteSparseImage(const wchar_t* path, ULONGLONG size) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER end{}; end.QuadPart = static_cast<LONGLONG>(size);
    bool ok = SetFilePointerEx(f, end, nullptr, FILE_BEGIN) && SetEndOfFile(f);
    BYTE header[0x1000]{};
    std::memcpy(header, "FATX", 4);
    header[8] = 2; header[12] = 1;
    LARGE_INTEGER zero{};
    ok = ok && SetFilePointerEx(f, zero, nullptr, FILE_BEGIN);
    DWORD written = 0;
    ok = ok && WriteFile(f, header, sizeof(header), &written, nullptr) && written == sizeof(header);
    BYTE eoc[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    LARGE_INTEGER fat{}; fat.QuadPart = 0x1004;
    ok = ok && SetFilePointerEx(f, fat, nullptr, FILE_BEGIN);
    written = 0;
    ok = ok && WriteFile(f, eoc, sizeof(eoc), &written, nullptr) && written == sizeof(eoc);
    CloseHandle(f);
    return ok;
}
static int Fail(const char* message) {
    std::fprintf(stderr, "FATX smoke test failed: %s\n", message);
    return 1;
}
int main() {
    constexpr DWORD imageSize = 64 * 1024, data = 0x2000, cluster = 1024, payloadSize = 1500;
    std::vector<BYTE> image(imageSize, 0);
    std::memcpy(image.data(), "FATX", 4);
    Put32(image, 8, 2); Put32(image, 12, 1);
    Put16(image, 0x1000 + 2, 0xFFFF); // root cluster
    Put16(image, 0x1000 + 4, 3); Put16(image, 0x1000 + 6, 0xFFFF);
    BYTE* root = image.data() + data;
    root[0] = 8; root[1] = FATX_ATTRIBUTE_SYSTEM | FATX_ATTRIBUTE_HIDDEN | FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(root + 2, "TEST.BIN", 8);
    Put32(image, data + 0x2C, 2); Put32(image, data + 0x30, payloadSize);
    root[64] = 0xFF;
    std::vector<BYTE> expected(payloadSize);
    for (DWORD i = 0; i < payloadSize; ++i) expected[i] = BYTE((i * 37u) % 251u);
    std::memcpy(image.data() + data + cluster, expected.data(), cluster);
    std::memcpy(image.data() + data + cluster * 2, expected.data() + cluster, payloadSize - cluster);

    wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"fxs", 0, path))
        return Fail("temporary image path unavailable");
    int result = 1;
    FATX_VOLUME volume = nullptr;
    if (!WriteImage(path, image)) { Fail("could not write image"); goto done; }
    if (ModuleStart() != FATX_OK) { Fail("ModuleStart failed"); goto done; }
    if (FATX_OpenVolumeW(path, imageSize - 512, 1024, &volume) != FATX_INVALID_ARGUMENT) {
        Fail("out-of-range partition accepted"); goto stop;
    }
    if (FATX_OpenVolumeW(path, 0, imageSize, &volume) != FATX_OK) {
        Fail("valid FATX image rejected"); goto stop;
    }
    {
        FATX_INFO info{};
        if (FATX_GetInfo(volume, &info) != FATX_OK || info.sectorSize != 512 ||
            info.sectorsPerCluster != 2 || info.clusterSize != cluster ||
            info.rootDirectoryCluster != 1 || info.fatEntrySize != 2 ||
            info.dataOffset != data) { Fail("volume geometry mismatch"); goto stop; }
    }
    {
        DWORD required = 0;
        if (FATX_ListDirectoryW(volume, L"\\", nullptr, 0, &required) != FATX_BUFFER_TOO_SMALL ||
            required != 1) { Fail("directory size query failed"); goto stop; }
        FATX_ENTRY entry{};
        if (FATX_ListDirectoryW(volume, L"\\", &entry, 1, &required) != FATX_OK ||
            std::wcscmp(entry.name, L"TEST.BIN") != 0 || entry.size != payloadSize ||
            entry.attributes != (FATX_ATTRIBUTE_SYSTEM | FATX_ATTRIBUTE_HIDDEN | FATX_ATTRIBUTE_ARCHIVE)) {
            Fail("directory listing mismatch"); goto stop;
        }
    }
    {
        std::vector<BYTE> actual(payloadSize); DWORD got = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 0, actual.data(), payloadSize, &got) != FATX_OK ||
            got != payloadSize || actual != expected) { Fail("multi-cluster read failed"); goto stop; }
        BYTE tail[500]{}; got = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 1000, tail, sizeof(tail), &got) != FATX_OK ||
            got != sizeof(tail) || std::memcmp(tail, expected.data() + 1000, sizeof(tail)) != 0) {
            Fail("cross-cluster partial read failed"); goto stop;
        }
        if (FATX_ReadFileW(volume, L"TEST.BIN", payloadSize, tail, sizeof(tail), &got) != FATX_OK || got != 0) {
            Fail("read past EOF was not bounded"); goto stop;
        }
    }
    FATX_CloseVolume(volume); volume = nullptr;
    // FATX 0xFFF8 is a reserved/media marker, not an end-of-chain marker.
    Put16(image, 0x1000 + 4, 0xFFF8);
    if (!WriteImage(path, image) || FATX_OpenVolumeW(path, 0, imageSize, &volume) != FATX_OK) {
        Fail("could not prepare reserved-marker fixture"); goto stop;
    }
    {
        BYTE out[payloadSize]{}; DWORD got = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 0, out, payloadSize, &got) != FATX_CORRUPT ||
            got != cluster) { Fail("reserved marker accepted as EOC"); goto stop; }
    }
    FATX_CloseVolume(volume); volume = nullptr;
    // The 64 MiB geometry crosses the 0xFFF0 cluster threshold and uses 32-bit FAT entries.
    if (!WriteSparseImage(path, 64ULL * 1024 * 1024) ||
        FATX_OpenVolumeW(path, 0, 64ULL * 1024 * 1024, &volume) != FATX_OK) {
        Fail("32-bit FAT geometry image rejected"); goto stop;
    }
    {
        FATX_INFO info{}; DWORD required = 99;
        if (FATX_GetInfo(volume, &info) != FATX_OK || info.fatEntrySize != 4 ||
            info.dataOffset != 0x42000 ||
            FATX_ListDirectoryW(volume, L"\\", nullptr, 0, &required) != FATX_OK ||
            required != 0) { Fail("32-bit FAT geometry mismatch"); goto stop; }
    }
    std::puts("PASS: FATX mount, partition bounds, geometry, directory listing, multi-cluster reads, EOF bounds, strict EOC markers, and 32-bit FAT");
    result = 0;
stop:
    if (volume) FATX_CloseVolume(volume);
    ModuleStop();
done:
    DeleteFileW(path);
    return result;
}
