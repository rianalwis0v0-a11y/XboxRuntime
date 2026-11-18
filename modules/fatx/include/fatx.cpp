
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include "../include/fatx.h"

#include <new>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#include <limits>

static bool g_initialized = false;
static std::mutex g_lifecycleMutex;
static unsigned long g_startReferences = 0;

struct FatxVolume {
    HANDLE file = INVALID_HANDLE_VALUE;
    ULONGLONG base = 0;
    ULONGLONG length = 0;
    bool writable = true;

    DWORD sectorsPerCluster = 0;
    DWORD clusterSize = 0;
    DWORD entrySize = 0;
    DWORD rootCluster = 0;

    ULONGLONG fatSize = 0;
    ULONGLONG dataOffset = 0;
    ULONGLONG clusterCount = 0;

    SRWLOCK lock = SRWLOCK_INIT;
};

struct FatxDirEntry {
    std::wstring name;
    DWORD attributes = 0;
    DWORD firstCluster = 0;
    DWORD size = 0;
};

static uint16_t Read16(const BYTE* p)
{
    return static_cast<uint16_t>(
        p[0] | (static_cast<uint16_t>(p[1]) << 8)
    );
}

static uint32_t Read32(const BYTE* p)
{
    return
        static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

static bool AddOverflow(
    ULONGLONG a,
    ULONGLONG b,
    ULONGLONG* result)
{
    if (a > UINT64_MAX - b)
        return true;

    *result = a + b;
    return false;
}

static bool AlignUp(
    ULONGLONG value,
    ULONGLONG alignment,
    ULONGLONG* result)
{
    if (!result || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        value > UINT64_MAX - (alignment - 1))
        return false;
    *result = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static bool ReadAt(
    FatxVolume* v,
    ULONGLONG offset,
    void* buffer,
    DWORD length)
{
    if (!v || !buffer)
        return false;

    ULONGLONG end = 0;

    if (AddOverflow(offset, length, &end) ||
        end > v->length)
        return false;

    ULONGLONG absolute = 0;

    if (AddOverflow(v->base, offset, &absolute) ||
        absolute > INT64_MAX)
        return false;

    AcquireSRWLockExclusive(&v->lock);

    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(absolute);

    bool ok = SetFilePointerEx(
        v->file,
        position,
        nullptr,
        FILE_BEGIN
    ) != FALSE;

    BYTE* destination = static_cast<BYTE*>(buffer);
    DWORD remaining = length;

    while (ok && remaining)
    {
        DWORD amount = std::min<DWORD>(
            remaining,
            1024 * 1024
        );

        DWORD received = 0;

        if (!ReadFile(
            v->file,
            destination,
            amount,
            &received,
            nullptr
        ) || received != amount)
        {
            ok = false;
            break;
        }

        destination += received;
        remaining -= received;
    }

    ReleaseSRWLockExclusive(&v->lock);
    return ok;
}

static bool WriteAt(FatxVolume* v, ULONGLONG offset, const void* buffer, DWORD length)
{
    if (!v || !v->writable || (!buffer && length)) return false;
    ULONGLONG end = 0, absolute = 0;
    if (AddOverflow(offset, length, &end) || end > v->length ||
        AddOverflow(v->base, offset, &absolute) || absolute > INT64_MAX) return false;
    AcquireSRWLockExclusive(&v->lock);
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(absolute);
    bool ok = SetFilePointerEx(v->file, position, nullptr, FILE_BEGIN) != FALSE;
    const BYTE* source = static_cast<const BYTE*>(buffer);
    DWORD remaining = length;
    while (ok && remaining) {
        DWORD amount = (std::min<DWORD>)(remaining, 1024 * 1024), written = 0;
        if (!WriteFile(v->file, source, amount, &written, nullptr) || written != amount) { ok = false; break; }
        source += written; remaining -= written;
    }
    if (ok && length && !FlushFileBuffers(v->file)) ok = false;
    ReleaseSRWLockExclusive(&v->lock);
    return ok;
}

static ULONGLONG ClusterOffset(
    const FatxVolume* v,
    DWORD cluster)
{
    return v->dataOffset +
        (static_cast<ULONGLONG>(cluster) - 1) *
        v->clusterSize;
}

static bool ValidCluster(
    const FatxVolume* v,
    DWORD cluster)
{
    return cluster >= 1 &&
        static_cast<ULONGLONG>(cluster) <=
        v->clusterCount;
}

static int NextCluster(
    FatxVolume* v,
    DWORD cluster,
    DWORD* next,
    bool* end)
{
    if (!ValidCluster(v, cluster) || !next || !end)
        return FATX_CORRUPT;

    ULONGLONG fatOffset =
        0x1000ULL +
        static_cast<ULONGLONG>(cluster) * v->entrySize;

    BYTE entry[4]{};

    if (!ReadAt(v, fatOffset, entry, v->entrySize))
        return FATX_IO_ERROR;

    uint32_t value =
        v->entrySize == 2
        ? Read16(entry)
        : Read32(entry);

    uint32_t endMarker =
        v->entrySize == 2
        ? 0xFFFF
        : 0xFFFFFFFF;

    uint32_t badMarker =
        v->entrySize == 2
        ? 0xFFF7
        : 0xFFFFFFF7;

    uint32_t reservedMarker =
        v->entrySize == 2
        ? 0xFFF0
        : 0xFFFFFFF0;

    if (value == endMarker)
    {
        *end = true;
        *next = 0;
        return FATX_OK;
    }

    if (value == badMarker ||
        value >= reservedMarker ||
        !ValidCluster(v, value))
        return FATX_CORRUPT;

    *next = value;
    *end = false;

    return FATX_OK;
}

static int ReadDirectory(
    FatxVolume* v,
    DWORD firstCluster,
    std::vector<FatxDirEntry>& output)
{
    if (!ValidCluster(v, firstCluster))
        return FATX_CORRUPT;

    std::unordered_set<DWORD> visited;
    std::vector<BYTE> clusterData(v->clusterSize);

    DWORD current = firstCluster;

    for (ULONGLONG count = 0;
         count < v->clusterCount;
         ++count)
    {
        if (!visited.insert(current).second)
            return FATX_CORRUPT;

        if (!ReadAt(
            v,
            ClusterOffset(v, current),
            clusterData.data(),
            v->clusterSize))
            return FATX_IO_ERROR;

        for (DWORD pos = 0;
             pos + 64 <= v->clusterSize;
             pos += 64)
        {
            const BYTE* entry = clusterData.data() + pos;

            if (entry[0] == 0xFF)
                return FATX_OK;

            if (entry[0] == 0xE5 || entry[0] == 0)
                continue;

            BYTE nameLength = entry[0];

            if (nameLength > 42)
                return FATX_CORRUPT;

            FatxDirEntry item;

            item.attributes = entry[1];
            item.firstCluster = Read32(entry + 0x2C);
            item.size = Read32(entry + 0x30);

            for (BYTE i = 0; i < nameLength; ++i)
            {
                BYTE character = entry[2 + i];

                if (character == 0 || character == 0xFF)
                    break;

                wchar_t decoded = static_cast<wchar_t>(character);
                if (character >= 0x80)
                {
                    const char encoded = static_cast<char>(character);
                    if (MultiByteToWideChar(
                        437, 0, &encoded, 1, &decoded, 1) != 1)
                        return FATX_CORRUPT;
                }
                item.name.push_back(decoded);
            }

            if (!item.name.empty())
                output.push_back(std::move(item));

            if (output.size() > 4096)
                return FATX_CORRUPT;
        }

        DWORD next = 0;
        bool end = false;

        int result = NextCluster(
            v, current, &next, &end
        );

        if (result != FATX_OK)
            return result;

        if (end)
            return FATX_OK;

        current = next;
    }

    return FATX_CORRUPT;
}

static std::vector<std::wstring> SplitPath(
    const wchar_t* path)
{
    std::vector<std::wstring> parts;

    if (!path)
        return parts;

    std::wstring value(path);

    if (value.size() > 240)
        return parts;

    size_t start = 0;

    while (start < value.size())
    {
        while (start < value.size() &&
            (value[start] == L'\\' ||
             value[start] == L'/'))
            ++start;

        size_t end = value.find_first_of(
            L"\\/",
            start
        );

        if (end == std::wstring::npos)
            end = value.size();

        if (end > start)
            parts.push_back(
                value.substr(start, end - start)
            );

        start = end + 1;
    }

    return parts;
}

static int ResolvePath(
    FatxVolume* v,
    const wchar_t* path,
    FatxDirEntry* result,
    bool* isRoot)
{
    if (!v || !path || !result || !isRoot)
        return FATX_INVALID_ARGUMENT;

    auto parts = SplitPath(path);

    DWORD directoryCluster = v->rootCluster;

    if (parts.empty())
    {
        *isRoot = true;
        result->attributes = FATX_ATTRIBUTE_DIRECTORY;
        result->firstCluster = directoryCluster;
        result->size = 0;
        return FATX_OK;
    }

    *isRoot = false;

    for (size_t index = 0;
         index < parts.size();
         ++index)
    {
        std::vector<FatxDirEntry> entries;

        int status = ReadDirectory(
            v,
            directoryCluster,
            entries
        );

        if (status != FATX_OK)
            return status;

        auto found = std::find_if(
            entries.begin(),
            entries.end(),
            [&](const FatxDirEntry& entry)
            {
                return _wcsicmp(
                    entry.name.c_str(),
                    parts[index].c_str()
                ) == 0;
            }
        );

        if (found == entries.end())
            return FATX_NOT_FOUND;

        *result = *found;

        if (index + 1 < parts.size())
        {
            if (!(result->attributes &
                FATX_ATTRIBUTE_DIRECTORY))
                return FATX_NOT_DIRECTORY;

            directoryCluster = result->firstCluster;
        }
    }

    return FATX_OK;
}

extern "C" int __cdecl FATX_ModuleStart()
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (g_startReferences++ == 0) {
        g_initialized = true;
        OutputDebugStringW(L"[XboxRuntime:FATX] FATX filesystem module ready\n");
    }
    return FATX_OK;
}

extern "C" void __cdecl FATX_ModuleStop()
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (g_startReferences == 0) return;
    if (--g_startReferences == 0) {
        g_initialized = false;
        OutputDebugStringW(L"[XboxRuntime:FATX] FATX filesystem module stopped\n");
    }
}

// The host module loader uses the common ModuleStart/ModuleStop ABI.
// Provider mounts hold their own FATX_ModuleStart reference independently.
#ifndef XBOX_FS_PROVIDER_TEST
extern "C" __declspec(dllexport) int __cdecl ModuleStart()
{
    return FATX_ModuleStart() == FATX_OK ? 0 : 1;
}
extern "C" __declspec(dllexport) void __cdecl ModuleStop()
{
    FATX_ModuleStop();
}
#endif

static int OpenVolumeWInternal(
    LPCWSTR devicePath, ULONGLONG partitionOffset,
    ULONGLONG partitionLength, FATX_VOLUME* output, bool writable)
{
    if (!g_initialized || !devicePath || !output)
        return FATX_INVALID_ARGUMENT;

    *output = nullptr;

    HANDLE file = CreateFileW(
        devicePath,
        GENERIC_READ | (writable ? GENERIC_WRITE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE)
        return FATX_IO_ERROR;

    ULONGLONG availableLength = 0;

    LARGE_INTEGER fileSize{};

    if (GetFileSizeEx(file, &fileSize) &&
        fileSize.QuadPart >= 0)
    {
        availableLength =
            static_cast<ULONGLONG>(fileSize.QuadPart);
    }
    else
    {
        GET_LENGTH_INFORMATION diskLength{};

        DWORD returned = 0;

        if (!DeviceIoControl(
            file,
            IOCTL_DISK_GET_LENGTH_INFO,
            nullptr,
            0,
            &diskLength,
            sizeof(diskLength),
            &returned,
            nullptr))
        {
            CloseHandle(file);
            return FATX_IO_ERROR;
        }

        availableLength =
            static_cast<ULONGLONG>(
                diskLength.Length.QuadPart
            );
    }

    if (partitionOffset > availableLength)
    {
        CloseHandle(file);
        return FATX_INVALID_ARGUMENT;
    }

    if (partitionLength == 0)
        partitionLength = availableLength - partitionOffset;

    if (partitionLength > availableLength - partitionOffset ||
        partitionLength < 0x2000)
    {
        CloseHandle(file);
        return FATX_INVALID_ARGUMENT;
    }

    FatxVolume* v = new (std::nothrow) FatxVolume();

    if (!v)
    {
        CloseHandle(file);
        return FATX_ERROR;
    }

    v->file = file;
    v->writable = writable;
    v->base = partitionOffset;
    v->length = partitionLength;

    BYTE header[0x1000]{};

    if (!ReadAt(v, 0, header, sizeof(header)) ||
        memcmp(header, "FATX", 4) != 0)
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    v->sectorsPerCluster = Read32(header + 8);
    v->rootCluster = Read32(header + 12);

    DWORD spc = v->sectorsPerCluster;

    if (spc == 0 || spc > 1024 ||
        (spc & (spc - 1)) != 0)
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    v->clusterSize = spc * 512;

    ULONGLONG maxClusters =
        partitionLength / v->clusterSize + 1;

    v->entrySize =
        maxClusters < 0xFFF0 ? 2 : 4;

    if (maxClusters >
        (UINT64_MAX - 0xFFF) / v->entrySize)
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    if (!AlignUp(
        maxClusters * v->entrySize,
        0x1000,
        &v->fatSize))
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    if (v->fatSize > partitionLength - 0x1000)
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    v->dataOffset = 0x1000 + v->fatSize;

    v->clusterCount =
        (partitionLength - v->dataOffset) /
        v->clusterSize;

    if (v->clusterCount == 0 ||
        v->clusterCount > 0x10000000 ||
        !ValidCluster(v, v->rootCluster))
    {
        delete v;
        CloseHandle(file);
        return FATX_INVALID_VOLUME;
    }

    *output = v;

    OutputDebugStringW(
        L"[XboxRuntime:FATX] FATX volume opened successfully\n"
    );

    return FATX_OK;
}

extern "C" int __cdecl FATX_OpenVolumeW(
    LPCWSTR path, ULONGLONG offset, ULONGLONG length, FATX_VOLUME* volume)
{
    return OpenVolumeWInternal(path, offset, length, volume, true);
}

extern "C" int __cdecl FATX_OpenVolumeReadOnlyW(
    LPCWSTR path, ULONGLONG offset, ULONGLONG length, FATX_VOLUME* volume)
{
    return OpenVolumeWInternal(path, offset, length, volume, false);
}

extern "C" void __cdecl FATX_CloseVolume(
    FATX_VOLUME handle)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);

    if (!v)
        return;

    if (v->file != INVALID_HANDLE_VALUE)
        CloseHandle(v->file);

    delete v;
}

extern "C" int __cdecl FATX_GetInfo(
    FATX_VOLUME handle,
    FATX_INFO* info)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);

    if (!g_initialized || !v || !info)
        return FATX_INVALID_ARGUMENT;

    info->sectorSize = 512;
    info->sectorsPerCluster = v->sectorsPerCluster;
    info->clusterSize = v->clusterSize;
    info->fatEntrySize = v->entrySize;
    info->rootDirectoryCluster = v->rootCluster;
    info->partitionSize = v->length;
    info->dataOffset = v->dataOffset;
    info->clusterCount = v->clusterCount;

    return FATX_OK;
}

extern "C" int __cdecl FATX_ListDirectoryW(
    FATX_VOLUME handle,
    LPCWSTR path,
    FATX_ENTRY* output,
    DWORD capacity,
    DWORD* required)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);

    if (!g_initialized || !v || !path || !required)
        return FATX_INVALID_ARGUMENT;

    *required = 0;

    FatxDirEntry directory;
    bool root = false;

    int status = ResolvePath(
        v, path, &directory, &root
    );

    if (status != FATX_OK)
        return status;

    if (!(directory.attributes &
        FATX_ATTRIBUTE_DIRECTORY))
        return FATX_NOT_DIRECTORY;

    std::vector<FatxDirEntry> entries;

    status = ReadDirectory(
        v,
        directory.firstCluster,
        entries
    );

    if (status != FATX_OK)
        return status;

    *required = static_cast<DWORD>(entries.size());

    if (capacity < entries.size() ||
        (!output && !entries.empty()))
        return FATX_BUFFER_TOO_SMALL;

    for (DWORD i = 0; i < entries.size(); ++i)
    {
        memset(&output[i], 0, sizeof(FATX_ENTRY));

        size_t count = std::min<size_t>(
            entries[i].name.size(),
            42
        );

        wmemcpy(
            output[i].name,
            entries[i].name.c_str(),
            count
        );

        output[i].attributes = entries[i].attributes;
        output[i].firstCluster = entries[i].firstCluster;
        output[i].size = entries[i].size;
    }

    return FATX_OK;
}

extern "C" int __cdecl FATX_ReadFileW(
    FATX_VOLUME handle,
    LPCWSTR path,
    ULONGLONG offset,
    void* buffer,
    DWORD bytesToRead,
    DWORD* bytesRead)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);

    if (!g_initialized || !v || !path ||
        !bytesRead || (!buffer && bytesToRead))
        return FATX_INVALID_ARGUMENT;

    *bytesRead = 0;

    FatxDirEntry entry;
    bool root = false;

    int status = ResolvePath(
        v, path, &entry, &root
    );

    if (status != FATX_OK)
        return status;

    if (root ||
        (entry.attributes & FATX_ATTRIBUTE_DIRECTORY))
        return FATX_IS_DIRECTORY;

    if (offset >= entry.size || bytesToRead == 0)
        return FATX_OK;

    ULONGLONG remaining =
        std::min<ULONGLONG>(
            bytesToRead,
            entry.size - offset
        );

    ULONGLONG skip = offset / v->clusterSize;
    DWORD inCluster =
        static_cast<DWORD>(offset % v->clusterSize);

    DWORD current = entry.firstCluster;

    if (entry.size && !ValidCluster(v, current))
        return FATX_CORRUPT;

    std::unordered_set<DWORD> visited;

    for (ULONGLONG i = 0; i < skip; ++i)
    {
        if (!visited.insert(current).second)
            return FATX_CORRUPT;

        DWORD next = 0;
        bool end = false;

        status = NextCluster(
            v, current, &next, &end
        );

        if (status != FATX_OK || end)
            return FATX_CORRUPT;

        current = next;
    }

    BYTE* destination = static_cast<BYTE*>(buffer);

    while (remaining)
    {
        if (!ValidCluster(v, current) ||
            visited.size() >= v->clusterCount ||
            !visited.insert(current).second)
            return FATX_CORRUPT;

        DWORD amount = static_cast<DWORD>(
            std::min<ULONGLONG>(
                remaining,
                v->clusterSize - inCluster
            )
        );

        ULONGLONG clusterPosition =
            ClusterOffset(v, current);

        ULONGLONG readPosition = 0;

        if (AddOverflow(
            clusterPosition,
            inCluster,
            &readPosition))
            return FATX_CORRUPT;

        if (!ReadAt(
            v,
            readPosition,
            destination,
            amount))
            return FATX_IO_ERROR;

        destination += amount;
        *bytesRead += amount;
        remaining -= amount;
        inCluster = 0;

        if (!remaining)
            break;

        DWORD next = 0;
        bool end = false;

        status = NextCluster(
            v, current, &next, &end
        );

        if (status != FATX_OK || end)
            return FATX_CORRUPT;

        current = next;
    }

    return FATX_OK;
}

extern "C" int __cdecl FATX_WriteFileW(
    FATX_VOLUME handle, LPCWSTR path, ULONGLONG offset,
    const void* buffer, DWORD bytesToWrite, DWORD* bytesWritten)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);
    if (!g_initialized || !v || !path || !bytesWritten || (!buffer && bytesToWrite))
        return FATX_INVALID_ARGUMENT;
    *bytesWritten = 0;
    FatxDirEntry entry; bool root = false;
    int status = ResolvePath(v, path, &entry, &root);
    if (status != FATX_OK) return status;
    if (root || (entry.attributes & FATX_ATTRIBUTE_DIRECTORY)) return FATX_IS_DIRECTORY;
    if (entry.attributes & FATX_ATTRIBUTE_READONLY) return FATX_INVALID_ARGUMENT;
    if (offset > entry.size || bytesToWrite > entry.size - offset) return FATX_INVALID_ARGUMENT;
    if (!bytesToWrite) return FATX_OK;
    ULONGLONG skip = offset / v->clusterSize;
    DWORD inCluster = static_cast<DWORD>(offset % v->clusterSize);
    DWORD current = entry.firstCluster;
    if (!ValidCluster(v, current)) return FATX_CORRUPT;
    std::unordered_set<DWORD> visited;
    for (ULONGLONG i = 0; i < skip; ++i) {
        if (!visited.insert(current).second) return FATX_CORRUPT;
        DWORD next = 0; bool end = false;
        status = NextCluster(v, current, &next, &end);
        if (status != FATX_OK || end) return FATX_CORRUPT;
        current = next;
    }
    const BYTE* source = static_cast<const BYTE*>(buffer);
    DWORD remaining = bytesToWrite;
    while (remaining) {
        if (!ValidCluster(v, current) || !visited.insert(current).second) return FATX_CORRUPT;
        DWORD amount = (std::min<DWORD>)(remaining, v->clusterSize - inCluster);
        ULONGLONG position = 0;
        if (AddOverflow(ClusterOffset(v, current), inCluster, &position)) return FATX_CORRUPT;
        if (!WriteAt(v, position, source, amount)) return FATX_IO_ERROR;
        source += amount; remaining -= amount; *bytesWritten += amount; inCluster = 0;
        if (remaining) {
            DWORD next = 0; bool end = false;
            status = NextCluster(v, current, &next, &end);
            if (status != FATX_OK || end) return FATX_CORRUPT;
            current = next;
        }
    }
    return FATX_OK;
}

extern "C" int __cdecl FATX_GetFileInfoW(FATX_VOLUME handle, LPCWSTR path, FATX_ENTRY* output)
{
    FatxVolume* v = static_cast<FatxVolume*>(handle);
    if (!g_initialized || !v || !path || !output) return FATX_INVALID_ARGUMENT;
    FatxDirEntry entry; bool root = false;
    const int status = ResolvePath(v, path, &entry, &root);
    if (status != FATX_OK) return status;
    std::memset(output, 0, sizeof(*output));
    if (root) {
        output->attributes = FATX_ATTRIBUTE_DIRECTORY;
        output->firstCluster = v->rootCluster;
        return FATX_OK;
    }
    const size_t count = (std::min<size_t>)(entry.name.size(), 42);
    std::wmemcpy(output->name, entry.name.c_str(), count);
    output->attributes = entry.attributes;
    output->firstCluster = entry.firstCluster;
    output->size = entry.size;
    return FATX_OK;
}
