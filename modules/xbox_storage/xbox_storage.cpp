#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <vector>
#include <cstring>
#include <cwchar>
#include <string>
#include "xbox_storage.h"

namespace {
bool Set(const wchar_t* name, const std::wstring& value) {
    return SetEnvironmentVariableW(name, value.empty() ? nullptr : value.c_str()) != FALSE;
}
bool Exists(const wchar_t* path) {
    if (!path || !*path) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}
std::wstring Number(ULONGLONG value) {
    wchar_t buffer[32]{};
    if (_ui64tow_s(value, buffer, _countof(buffer), 16) != 0) return {};
    return std::wstring(L"0x") + buffer;
}

bool ReadAt(HANDLE disk, ULONGLONG offset, void* buffer, DWORD bytes) {
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(disk, position, nullptr, FILE_BEGIN)) return false;
    DWORD read = 0;
    return ReadFile(disk, buffer, bytes, &read, nullptr) && read == bytes;
}
bool IsFatxPartition(HANDLE disk, ULONGLONG offset, ULONGLONG length) {
    if (length < 0x2000) return false;
    BYTE header[0x1000]{};
    if (!ReadAt(disk, offset, header, sizeof(header)) ||
        std::memcmp(header, "FATX", 4) != 0) return false;
    const DWORD sectorsPerCluster = DWORD(header[8]) | (DWORD(header[9]) << 8) |
        (DWORD(header[10]) << 16) | (DWORD(header[11]) << 24);
    const DWORD rootCluster = DWORD(header[12]) | (DWORD(header[13]) << 8) |
        (DWORD(header[14]) << 16) | (DWORD(header[15]) << 24);
    return sectorsPerCluster && sectorsPerCluster <= 1024 &&
        !(sectorsPerCluster & (sectorsPerCluster - 1)) && rootCluster != 0;
}
bool ConfigureFatxRaw(const std::wstring& path, const std::wstring& device,
    ULONGLONG offset, ULONGLONG length) {
    return Set(L"XBOX_RUNTIME_FATX_IMAGE", path) &&
        Set(L"XBOX_RUNTIME_FATX_DEVICE", device) &&
        Set(L"XBOX_RUNTIME_FATX_OFFSET", Number(offset)) &&
        Set(L"XBOX_RUNTIME_FATX_LENGTH", Number(length)) &&
        Set(L"XBOX_RUNTIME_FATX_READ_ONLY", L"1");
}
bool IsXdvdfsOptical(HANDLE drive) {
    BYTE descriptor[2048]{};
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    // Raw XISO images start at byte zero; original Xbox XGD1 media can
    // carry the XDVDFS volume at the known game-partition base.
    const ULONGLONG bases[] = { 0, 0x18300000ull };
    for (ULONGLONG base : bases) {
        if (base > UINT64_MAX - 32ull * 2048ull) continue;
        if (ReadAt(drive, base + 32ull * 2048ull, descriptor, sizeof(descriptor)) &&
            std::memcmp(descriptor, magic, 20) == 0 &&
            std::memcmp(descriptor + 0x7EC, magic, 20) == 0)
            return true;
    }
    return false;
}
}

extern "C" int __cdecl XboxStorageSetFatxImage(
    const wchar_t* imagePath, const wchar_t* deviceName,
    ULONGLONG partitionOffset, ULONGLONG partitionLength) {
    if (!Exists(imagePath) || (deviceName && !*deviceName)) return ERROR_INVALID_PARAMETER;
    std::wstring offset = Number(partitionOffset);
    if (offset.empty()) return ERROR_INVALID_PARAMETER;
    if (!partitionLength) {
        HANDLE file = CreateFileW(imagePath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return GetLastError();
        LARGE_INTEGER size{};
        const BOOL ok = GetFileSizeEx(file, &size);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!ok || size.QuadPart < 0 ||
            partitionOffset > static_cast<ULONGLONG>(size.QuadPart))
            return ok ? ERROR_INVALID_PARAMETER : error;
        partitionLength = static_cast<ULONGLONG>(size.QuadPart) - partitionOffset;
    }
    if (!partitionLength) return ERROR_INVALID_PARAMETER;
    const std::wstring length = Number(partitionLength);
    if (length.empty()) return ERROR_INVALID_PARAMETER;
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_READ_ONLY", nullptr);
    if (!Set(L"XBOX_RUNTIME_FATX_IMAGE", imagePath) ||
        !Set(L"XBOX_RUNTIME_FATX_DEVICE", deviceName ? deviceName : L"\\Device\\Harddisk0\\Partition1") ||
        !Set(L"XBOX_RUNTIME_FATX_OFFSET", offset) ||
        !Set(L"XBOX_RUNTIME_FATX_LENGTH", length)) {
        const DWORD error = GetLastError() ? GetLastError() : ERROR_INVALID_PARAMETER;
        SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_IMAGE", nullptr);
        SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_DEVICE", nullptr);
        SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_OFFSET", nullptr);
        SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_LENGTH", nullptr);
        return error;
    }
    return ERROR_SUCCESS;
}

extern "C" int __cdecl XboxStorageSetXdvdfsImage(
    const wchar_t* imagePath, const wchar_t* deviceName) {
    if (!Exists(imagePath) || (deviceName && !*deviceName)) return ERROR_INVALID_PARAMETER;
    if (!Set(L"XBOX_RUNTIME_XDVDFS_IMAGE", imagePath) ||
        !Set(L"XBOX_RUNTIME_XDVDFS_DEVICE", deviceName ? deviceName : L"\\Device\\CdRom0")) {
        const DWORD error = GetLastError() ? GetLastError() : ERROR_INVALID_PARAMETER;
        SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", nullptr);
        SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_DEVICE", nullptr);
        return error;
    }
    return ERROR_SUCCESS;
}

extern "C" void __cdecl XboxStorageClearConfiguration(void) {
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_IMAGE", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_DEVICE", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_OFFSET", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_LENGTH", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_READ_ONLY", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_AUTO_COUNT", nullptr);
    for (DWORD i = 0; i < 16; ++i) {
        wchar_t key[96]{};
        swprintf_s(key, L"XBOX_RUNTIME_FATX_AUTO_%lu_IMAGE", i); SetEnvironmentVariableW(key, nullptr);
        swprintf_s(key, L"XBOX_RUNTIME_FATX_AUTO_%lu_DEVICE", i); SetEnvironmentVariableW(key, nullptr);
        swprintf_s(key, L"XBOX_RUNTIME_FATX_AUTO_%lu_OFFSET", i); SetEnvironmentVariableW(key, nullptr);
        swprintf_s(key, L"XBOX_RUNTIME_FATX_AUTO_%lu_LENGTH", i); SetEnvironmentVariableW(key, nullptr);
    }
    SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_DEVICE", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_AUTO", nullptr);
}

extern "C" int __cdecl XboxStorageDiscoverAndConfigure(DWORD* discoveredVolumes) {
    if (!discoveredVolumes) return ERROR_INVALID_PARAMETER;
    *discoveredVolumes = 0;

    // Explicit caller configuration wins. MainStart invokes discovery
    // automatically, but must never replace an image/device chosen by a launcher.
    const bool explicitFatx =
        GetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_IMAGE", nullptr, 0) != 0;
    const bool explicitDisc =
        GetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", nullptr, 0) != 0;
    if (explicitFatx || explicitDisc) {
        *discoveredVolumes = (explicitFatx ? 1u : 0u) + (explicitDisc ? 1u : 0u);
        return ERROR_SUCCESS;
    }
    XboxStorageClearConfiguration();

    // Search every physical disk read-only. Up to 16 validated FATX partitions
    // can be handed to the guest; none is writable by default.
    DWORD fatxCount = 0;
    for (DWORD diskNumber = 0; diskNumber < 32 && fatxCount < 16; ++diskNumber) {
        wchar_t rawPath[64]{};
        swprintf_s(rawPath, L"\\\\.\\PhysicalDrive%lu", diskNumber);
        HANDLE disk = CreateFileW(rawPath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (disk == INVALID_HANDLE_VALUE) continue;
        std::vector<BYTE> layoutBytes(64 * 1024);
        DWORD returned = 0;
        if (DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
            nullptr, 0, layoutBytes.data(), static_cast<DWORD>(layoutBytes.size()),
            &returned, nullptr) &&
            returned >= FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) {
            const auto* layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(layoutBytes.data());
            const size_t headerBytes = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
            const size_t entryBytes = size_t(layout->PartitionCount) * sizeof(PARTITION_INFORMATION_EX);
            if (entryBytes <= returned - headerBytes) {
                for (DWORD i = 0; i < layout->PartitionCount && fatxCount < 16; ++i) {
                    const auto& part = layout->PartitionEntry[i];
                    if (part.PartitionNumber == 0 || part.StartingOffset.QuadPart < 0 ||
                        part.PartitionLength.QuadPart < 0x2000) continue;
                    const ULONGLONG offset = static_cast<ULONGLONG>(part.StartingOffset.QuadPart);
                    const ULONGLONG length = static_cast<ULONGLONG>(part.PartitionLength.QuadPart);
                    if (!IsFatxPartition(disk, offset, length)) continue;
                    wchar_t ntName[96]{};
                    swprintf_s(ntName, L"\\Device\\Harddisk%lu\\Partition%lu",
                        diskNumber, part.PartitionNumber);
                    wchar_t key[96]{};
                    const auto setIndexed = [&](const wchar_t* suffix, const std::wstring& value) {
                        swprintf_s(key, L"XBOX_RUNTIME_FATX_AUTO_%lu_%s", fatxCount, suffix);
                        return Set(key, value);
                    };
                    if (setIndexed(L"IMAGE", rawPath) && setIndexed(L"DEVICE", ntName) &&
                        setIndexed(L"OFFSET", Number(offset)) && setIndexed(L"LENGTH", Number(length)))
                        ++fatxCount;
                }
            }
        }
        CloseHandle(disk);
    }
    if (fatxCount) {
        wchar_t count[16]{};
        swprintf_s(count, L"%lu", fatxCount);
        Set(L"XBOX_RUNTIME_FATX_AUTO_COUNT", count);
        *discoveredVolumes = fatxCount;
    }

    // Probe mounted optical drive letters for an XDVDFS volume descriptor.
    const DWORD logicalDrives = GetLogicalDrives();
    bool opticalMounted = false;
    for (DWORD index = 0; index < 26 && !opticalMounted; ++index) {
        if (!(logicalDrives & (1u << index))) continue;
        wchar_t root[] = L"A:\\";
        root[0] = static_cast<wchar_t>(L'A' + index);
        if (GetDriveTypeW(root) != DRIVE_CDROM) continue;
        wchar_t rawPath[] = L"\\\\.\\A:";
        rawPath[4] = static_cast<wchar_t>(L'A' + index);
        HANDLE drive = CreateFileW(rawPath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE) continue;
        const bool valid = IsXdvdfsOptical(drive);
        CloseHandle(drive);
        if (!valid) continue;
        opticalMounted = Set(L"XBOX_RUNTIME_XDVDFS_IMAGE", rawPath) &&
            Set(L"XBOX_RUNTIME_XDVDFS_DEVICE", L"\\Device\\CdRom0") &&
            Set(L"XBOX_RUNTIME_XDVDFS_AUTO", L"1");
        if (opticalMounted) ++*discoveredVolumes;
    }
    return *discoveredVolumes ? ERROR_SUCCESS : ERROR_NOT_FOUND;
}

extern "C" int __cdecl ModuleStart(void) {
    OutputDebugStringW(L"[XboxRuntime:Storage] Explicit image-backed storage configuration ready.\n");
    return ERROR_SUCCESS;
}
extern "C" void __cdecl ModuleStop(void) {}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
