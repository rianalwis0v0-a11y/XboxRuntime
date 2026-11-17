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
    Put16(image, 0x1000 + 8, 0xFFFF); // MEDIA directory cluster
    Put16(image, 0x1000 + 10, 0xFFFF); // nested file cluster
    Put16(image, 0x1000 + 12, 0xFFFF); // CP437 filename cluster
    BYTE* root = image.data() + data;
    root[0] = 8; root[1] = FATX_ATTRIBUTE_SYSTEM | FATX_ATTRIBUTE_HIDDEN | FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(root + 2, "TEST.BIN", 8);
    Put32(image, data + 0x2C, 2); Put32(image, data + 0x30, payloadSize);
    root[64] = 5; root[65] = FATX_ATTRIBUTE_DIRECTORY;
    std::memcpy(root + 66, "MEDIA", 5);
    Put32(image, data + 64 + 0x2C, 4);
    root[128] = 8; root[129] = FATX_ATTRIBUTE_ARCHIVE;
    const BYTE cp437Name[8] = {'C','A','F',0x82,'.','T','X','T'};
    std::memcpy(root + 130, cp437Name, sizeof(cp437Name));
    Put32(image, data + 128 + 0x2C, 6);
    Put32(image, data + 128 + 0x30, 2);
    root[192] = 0xFF;
    BYTE* media = image.data() + data + cluster * 3;
    media[0] = 9; media[1] = FATX_ATTRIBUTE_ARCHIVE;
    std::memcpy(media + 2, "INNER.TXT", 9);
    Put32(image, data + cluster * 3 + 0x2C, 5);
    Put32(image, data + cluster * 3 + 0x30, 4);
    media[64] = 0xFF;
    std::memcpy(image.data() + data + cluster * 4, "nest", 4);
    std::memcpy(image.data() + data + cluster * 5, "ok", 2);
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
            required != 3) { Fail("directory size query failed"); goto stop; }
        FATX_ENTRY entries[3]{};
        if (FATX_ListDirectoryW(volume, L"\\", entries, 3, &required) != FATX_OK ||
            std::wcscmp(entries[0].name, L"TEST.BIN") != 0 || entries[0].size != payloadSize ||
            entries[0].attributes != (FATX_ATTRIBUTE_SYSTEM | FATX_ATTRIBUTE_HIDDEN | FATX_ATTRIBUTE_ARCHIVE) ||
            std::wcscmp(entries[1].name, L"MEDIA") != 0 ||
            !(entries[1].attributes & FATX_ATTRIBUTE_DIRECTORY) ||
            std::wcscmp(entries[2].name, L"CAF\\u00C9.TXT") != 0) {
            Fail("root directory listing mismatch"); goto stop;
        }
        if (FATX_ListDirectoryW(volume, L"media", nullptr, 0, &required) != FATX_BUFFER_TOO_SMALL ||
            required != 1) { Fail("nested directory lookup failed"); goto stop; }
        BYTE cp437[2]{}; DWORD cp437Read = 0;
        if (FATX_ReadFileW(volume, L"caf\\u00E9.txt", 0, cp437, sizeof(cp437), &cp437Read) != FATX_OK ||
            cp437Read != 2 || std::memcmp(cp437, "ok", 2) != 0) {
            Fail("CP437 filename decoding failed"); goto stop;
        }
        BYTE nested[4]{}; DWORD nestedRead = 0;
        if (FATX_ReadFileW(volume, L"MEDIA/inner.txt", 0, nested, sizeof(nested), &nestedRead) != FATX_OK ||
            nestedRead != 4 || std::memcmp(nested, "nest", 4) != 0) {
            Fail("case-insensitive nested path read failed"); goto stop;
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
    // A cyclic chain must stop after the first cluster instead of looping.
    Put16(image, 0x1000 + 4, 2);
    if (!WriteImage(path, image) || FATX_OpenVolumeW(path, 0, imageSize, &volume) != FATX_OK) {
        Fail("could not prepare cyclic-chain fixture"); goto stop;
    }
    {
        BYTE out[payloadSize]{}; DWORD got = 0;
        if (FATX_ReadFileW(volume, L"TEST.BIN", 0, out, payloadSize, &got) != FATX_CORRUPT ||
            got != cluster) { Fail("cyclic FAT chain was not detected"); goto stop; }
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
