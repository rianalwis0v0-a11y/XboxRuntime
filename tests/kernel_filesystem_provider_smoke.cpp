#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kernel_filesystem_bridge.h"
#include "modules/fatx/fatx_provider.h"
#include "modules/xdvdfs/xdvdfs_provider.h"

static void Put16(std::vector<BYTE>& b, size_t o, uint16_t v) { b[o] = BYTE(v); b[o+1] = BYTE(v >> 8); }
static void Put32(std::vector<BYTE>& b, size_t o, uint32_t v) { for (int i=0;i<4;++i) b[o+i] = BYTE(v >> (i*8)); }
static bool WriteImage(const WCHAR* path, const std::vector<BYTE>& bytes) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0; const bool ok = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(f); return ok;
}
static int Fail(const char* message) { std::fprintf(stderr, "filesystem provider smoke failed: %s\n", message); return 1; }

int main() {
    WCHAR temp[MAX_PATH]{}, fatxPath[MAX_PATH]{}, xisoPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"fpx", 0, fatxPath) ||
        !GetTempFileNameW(temp, L"xpx", 0, xisoPath)) return Fail("temporary paths unavailable");

    std::vector<BYTE> fatx(64 * 1024, 0);
    std::memcpy(fatx.data(), "FATX", 4); Put32(fatx, 8, 2); Put32(fatx, 12, 1);
    Put16(fatx, 0x1000 + 2, 0xFFFF); Put16(fatx, 0x1000 + 4, 3); Put16(fatx, 0x1000 + 6, 0xFFFF);
    const size_t data = 0x2000;
    BYTE* root = fatx.data() + data;
    root[0] = 8; root[1] = 0x20; std::memcpy(root + 2, "TEST.BIN", 8);
    Put32(fatx, data + 0x2C, 2); Put32(fatx, data + 0x30, 1500); root[64] = 0xFF;
    std::memset(fatx.data() + data + 1024, 'A', 1024);
    std::memset(fatx.data() + data + 2048, 'B', 1024);
    std::memcpy(fatx.data() + data + 1024, "fatxdata", 8);
    if (!WriteImage(fatxPath, fatx)) return Fail("FATX fixture write failed");

    XBOX_FS_MOUNT fatxMount = nullptr;
    if (FATX_RegisterFilesystemProvider(L"\\Device\\Harddisk0\\Partition1",
        fatxPath, 0, fatx.size(), &fatxMount) != 0 || !fatxMount)
        return Fail("FATX provider mount failed");
    XBOX_FS_MOUNT fatxMount2 = nullptr;
    if (FATX_RegisterFilesystemProvider(L"\\Device\\Harddisk0\\Partition2",
        fatxPath, 0, fatx.size(), &fatxMount2) != 0 || !fatxMount2)
        return Fail("second FATX provider mount failed");
    XBOX_FS_FILE secondFatxFile = nullptr;
    if (XboxFsOpen(L"\\Device\\Harddisk0\\Partition2\\TEST.BIN",
        GENERIC_READ, 0, &secondFatxFile) != 0 || !secondFatxFile)
        return Fail("second FATX mount open failed");

    XBOX_FS_FILE file = nullptr;
    if (XboxFsOpen(L"\\Device\\Harddisk0\\Partition1\\TEST.BIN",
        GENERIC_READ | GENERIC_WRITE, 0, &file) != 0 || !file)
        return Fail("FATX bridge open failed");
    XBOX_FS_FILE_INFO info{};
    if (XboxFsQueryInformation(file, &info) != 0 || info.size != 1500 || info.allocationSize != 2048)
        return Fail("FATX query information mismatch");
    BYTE readback[8]{}; ULONG transferred = 0;
    if (XboxFsRead(file, 0, readback, sizeof(readback), &transferred) != 0 ||
        transferred != 8 || std::memcmp(readback, "fatxdata", 8) != 0)
        return Fail("FATX bridge read failed");
    if (FATX_UnregisterFilesystemProvider(fatxMount) != static_cast<LONG>(0x80000011u))
        return Fail("FATX unmounted with an open file");
    if (XboxFsWrite(file, 0, "write!!!", 8, &transferred) != 0 || transferred != 8)
        return Fail("FATX bridge write failed");
    std::memset(readback, 0, sizeof(readback));
    if (XboxFsRead(file, 0, readback, sizeof(readback), &transferred) != 0 ||
        transferred != 8 || std::memcmp(readback, "write!!!", 8) != 0)
        return Fail("FATX write was not persisted");
    BYTE patch[500]; std::memset(patch, 'W', sizeof(patch));
    if (XboxFsWrite(file, 1000, patch, sizeof(patch), &transferred) != 0 ||
        transferred != sizeof(patch))
        return Fail("FATX cross-cluster write failed");
    BYTE verify[500]{}; transferred = 0;
    if (XboxFsRead(file, 1000, verify, sizeof(verify), &transferred) != 0 ||
        transferred != sizeof(verify) || std::memcmp(verify, patch, sizeof(patch)) != 0)
        return Fail("FATX cross-cluster write was not persisted");
    transferred = 99;
    if (XboxFsWrite(file, 1499, patch, 2, &transferred) != static_cast<LONG>(0xC000000Du) ||
        transferred != 0)
        return Fail("FATX out-of-range write was accepted");
    XboxFsClose(file);
    if (FATX_UnregisterFilesystemProvider(fatxMount) != 0)
        return Fail("FATX provider unmount failed");
    // Removing one FATX mount must not stop the shared module while another
    // independently mounted volume still has an open provider handle.
    std::memset(readback, 0, sizeof(readback));
    if (XboxFsRead(secondFatxFile, 0, readback, sizeof(readback), &transferred) != 0 ||
        transferred != 8 || std::memcmp(readback, "write!!!", 8) != 0)
        return Fail("remaining FATX mount stopped when sibling unmounted");
    XboxFsClose(secondFatxFile);
    if (FATX_UnregisterFilesystemProvider(fatxMount2) != 0)
        return Fail("second FATX provider unmount failed");

    constexpr size_t sector = 2048;
    std::vector<BYTE> xiso(64 * sector, 0xFF);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    const size_t descriptor = 32 * sector, rootOffset = 40 * sector;
    std::memcpy(xiso.data() + descriptor, magic, 20);
    Put32(xiso, descriptor + 0x14, 40); Put32(xiso, descriptor + 0x18, 24);
    std::memcpy(xiso.data() + descriptor + 0x7EC, magic, 20);
    Put16(xiso, rootOffset, 0xFFFF); Put16(xiso, rootOffset + 2, 0xFFFF);
    Put32(xiso, rootOffset + 4, 50); Put32(xiso, rootOffset + 8, 8);
    xiso[rootOffset + 12] = 0x20; xiso[rootOffset + 13] = 8;
    std::memcpy(xiso.data() + rootOffset + 14, "DISC.BIN", 8);
    std::memcpy(xiso.data() + 50 * sector, "discdata", 8);
    if (!WriteImage(xisoPath, xiso)) return Fail("XISO fixture write failed");

    XBOX_FS_MOUNT discMount = nullptr;
    if (XDVDFS_RegisterFilesystemProvider(L"\\Device\\CdRom0", xisoPath, &discMount) != 0 || !discMount)
        return Fail("XDVDFS provider mount failed");
    XBOX_FS_MOUNT secondDiscMount = nullptr;
    if (XDVDFS_RegisterFilesystemProvider(L"\\Device\\CdRom1", xisoPath, &secondDiscMount) !=
        static_cast<LONG>(0x80000011u) || secondDiscMount)
        return Fail("XDVDFS accepted a second mount despite its single-volume backend");
    file = nullptr;
    if (XboxFsOpen(L"\\Device\\CdRom0\\DISC.BIN", GENERIC_READ, 0, &file) != 0 || !file)
        return Fail("XDVDFS bridge open failed");
    std::memset(&info, 0, sizeof(info));
    if (XboxFsQueryInformation(file, &info) != 0 || info.size != 8 || info.allocationSize != sector)
        return Fail("XDVDFS query information mismatch");
    std::memset(readback, 0, sizeof(readback));
    if (XboxFsRead(file, 0, readback, sizeof(readback), &transferred) != 0 ||
        transferred != 8 || std::memcmp(readback, "discdata", 8) != 0)
        return Fail("XDVDFS bridge read failed");
    if (XboxFsWrite(file, 0, "nope", 4, &transferred) != static_cast<LONG>(0xC0000022u))
        return Fail("XDVDFS write was not denied by read-only mount");
    XboxFsClose(file);
    if (XDVDFS_UnregisterFilesystemProvider(discMount) != 0)
        return Fail("XDVDFS provider unmount failed");

    DeleteFileW(fatxPath); DeleteFileW(xisoPath);
    std::puts("PASS: FATX provider mount/open/read/write/query/close/busy-unmount and XDVDFS read-only mount/open/read/query/close");
    return 0;
}
