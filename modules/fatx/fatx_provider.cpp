#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <new>
#include <string>
#include <mutex>
#include <unordered_map>
#include "guest32/kernel_filesystem_bridge.h"
#include "fatx.h"

namespace {
constexpr LONG STATUS_SUCCESS_VALUE = 0;
constexpr LONG STATUS_INVALID_PARAMETER_VALUE = static_cast<LONG>(0xC000000D);
constexpr LONG STATUS_NO_MEMORY_VALUE = static_cast<LONG>(0xC0000017);
constexpr LONG STATUS_ACCESS_DENIED_VALUE = static_cast<LONG>(0xC0000022);
constexpr LONG STATUS_OBJECT_NAME_NOT_FOUND_VALUE = static_cast<LONG>(0xC0000034);
constexpr LONG STATUS_NOT_A_DIRECTORY_VALUE = static_cast<LONG>(0xC0000103);
constexpr LONG STATUS_FILE_IS_A_DIRECTORY_VALUE = static_cast<LONG>(0xC00000BA);
constexpr LONG STATUS_FILE_CORRUPT_ERROR_VALUE = static_cast<LONG>(0xC0000102);
constexpr LONG STATUS_IO_DEVICE_ERROR_VALUE = static_cast<LONG>(0xC0000185);
constexpr ACCESS_MASK READ_MASK = 0x80000000u | 0x00000001u;
constexpr ACCESS_MASK WRITE_MASK = 0x40000000u | 0x00000002u | 0x00000004u | 0x00010000u | 0x10000000u;

struct FatxProviderContext { FATX_VOLUME volume = nullptr; XBOX_FS_MOUNT mount = nullptr; };
std::mutex g_contextMutex;
std::unordered_map<XBOX_FS_MOUNT, FatxProviderContext*> g_contexts;
struct FatxProviderFile { std::wstring path; FATX_ENTRY entry{}; };

LONG MapStatus(int status) {
    switch (status) {
    case FATX_OK: return STATUS_SUCCESS_VALUE;
    case FATX_NOT_FOUND: return STATUS_OBJECT_NAME_NOT_FOUND_VALUE;
    case FATX_NOT_DIRECTORY: return STATUS_NOT_A_DIRECTORY_VALUE;
    case FATX_IS_DIRECTORY: return STATUS_FILE_IS_A_DIRECTORY_VALUE;
    case FATX_INVALID_ARGUMENT: return STATUS_INVALID_PARAMETER_VALUE;
    case FATX_CORRUPT: return STATUS_FILE_CORRUPT_ERROR_VALUE;
    case FATX_IO_ERROR: case FATX_INVALID_VOLUME: return STATUS_IO_DEVICE_ERROR_VALUE;
    default: return STATUS_IO_DEVICE_ERROR_VALUE;
    }
}
LONG __stdcall ProviderOpen(void* opaque, const WCHAR* path, ACCESS_MASK access,
    ULONG, void** output) {
    if (output) *output = nullptr;
    auto* context = static_cast<FatxProviderContext*>(opaque);
    if (!context || !context->volume || !path || !output) return STATUS_INVALID_PARAMETER_VALUE;
    if (!(access & (READ_MASK | WRITE_MASK))) return STATUS_ACCESS_DENIED_VALUE;
    if (wcslen(path) > 240) return STATUS_INVALID_PARAMETER_VALUE;
    auto* file = new (std::nothrow) FatxProviderFile();
    if (!file) return STATUS_NO_MEMORY_VALUE;
    file->path = path;
    std::replace(file->path.begin(), file->path.end(), L'/', L'\\');
    const int result = FATX_GetFileInfoW(context->volume, file->path.c_str(), &file->entry);
    if (result != FATX_OK) { delete file; return MapStatus(result); }
    if (file->entry.attributes & FATX_ATTRIBUTE_DIRECTORY) { delete file; return STATUS_FILE_IS_A_DIRECTORY_VALUE; }
    if ((access & WRITE_MASK) && (file->entry.attributes & FATX_ATTRIBUTE_READONLY)) {
        delete file; return STATUS_ACCESS_DENIED_VALUE;
    }
    *output = file;
    return STATUS_SUCCESS_VALUE;
}
LONG __stdcall ProviderRead(void* opaque, void* fileOpaque, ULONGLONG offset,
    void* buffer, ULONG length, ULONG* transferred) {
    auto* context = static_cast<FatxProviderContext*>(opaque);
    auto* file = static_cast<FatxProviderFile*>(fileOpaque);
    if (!context || !context->volume || !file || !transferred || (!buffer && length))
        return STATUS_INVALID_PARAMETER_VALUE;
    *transferred = 0;
    return MapStatus(FATX_ReadFileW(context->volume, file->path.c_str(),
        offset, buffer, length, transferred));
}
LONG __stdcall ProviderWrite(void* opaque, void* fileOpaque, ULONGLONG offset,
    const void* buffer, ULONG length, ULONG* transferred) {
    auto* context = static_cast<FatxProviderContext*>(opaque);
    auto* file = static_cast<FatxProviderFile*>(fileOpaque);
    if (!context || !context->volume || !file || !transferred || (!buffer && length))
        return STATUS_INVALID_PARAMETER_VALUE;
    *transferred = 0;
    if (file->entry.attributes & FATX_ATTRIBUTE_READONLY) return STATUS_ACCESS_DENIED_VALUE;
    return MapStatus(FATX_WriteFileW(context->volume, file->path.c_str(),
        offset, buffer, length, transferred));
}
LONG __stdcall ProviderQuery(void* opaque, void* fileOpaque, XBOX_FS_FILE_INFO* info) {
    auto* context = static_cast<FatxProviderContext*>(opaque);
    auto* file = static_cast<FatxProviderFile*>(fileOpaque);
    if (!context || !context->volume || !file || !info) return STATUS_INVALID_PARAMETER_VALUE;
    FATX_ENTRY current{};
    const int status = FATX_GetFileInfoW(context->volume, file->path.c_str(), &current);
    if (status != FATX_OK) return MapStatus(status);
    FATX_INFO volume{};
    if (FATX_GetInfo(context->volume, &volume) != FATX_OK) return STATUS_IO_DEVICE_ERROR_VALUE;
    info->size = current.size;
    info->allocationSize = current.size
        ? ((current.size + volume.clusterSize - 1) / volume.clusterSize) * volume.clusterSize : 0;
    DWORD attrs = 0;
    if (current.attributes & FATX_ATTRIBUTE_READONLY) attrs |= FILE_ATTRIBUTE_READONLY;
    if (current.attributes & FATX_ATTRIBUTE_HIDDEN) attrs |= FILE_ATTRIBUTE_HIDDEN;
    if (current.attributes & FATX_ATTRIBUTE_SYSTEM) attrs |= FILE_ATTRIBUTE_SYSTEM;
    if (current.attributes & FATX_ATTRIBUTE_DIRECTORY) attrs |= FILE_ATTRIBUTE_DIRECTORY;
    if (current.attributes & FATX_ATTRIBUTE_ARCHIVE) attrs |= FILE_ATTRIBUTE_ARCHIVE;
    info->attributes = attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
    info->reserved = 0;
    return STATUS_SUCCESS_VALUE;
}
void __stdcall ProviderClose(void*, void* fileOpaque) {
    delete static_cast<FatxProviderFile*>(fileOpaque);
}
const XBOX_FS_PROVIDER_V1 kProvider = {
    XBOX_FS_PROVIDER_ABI_V1, sizeof(XBOX_FS_PROVIDER_V1), L"FATX",
    ProviderOpen, ProviderRead, ProviderWrite, ProviderQuery, ProviderClose
};
}

extern "C" __declspec(dllexport) LONG __stdcall FATX_RegisterFilesystemProvider(
    const WCHAR* deviceName, const WCHAR* backingDevicePath,
    ULONGLONG partitionOffset, ULONGLONG partitionLength, XBOX_FS_MOUNT* mountHandle) {
    if (!deviceName || !*deviceName || !backingDevicePath || !*backingDevicePath || !mountHandle)
        return STATUS_INVALID_PARAMETER_VALUE;
    *mountHandle = nullptr;
    if (FATX_ModuleStart() != FATX_OK) return STATUS_IO_DEVICE_ERROR_VALUE;
    auto* context = new (std::nothrow) FatxProviderContext();
    if (!context) { FATX_ModuleStop(); return STATUS_NO_MEMORY_VALUE; }
    const int openStatus = FATX_OpenVolumeW(backingDevicePath, partitionOffset, partitionLength, &context->volume);
    if (openStatus != FATX_OK) { delete context; FATX_ModuleStop(); return MapStatus(openStatus); }
    const LONG status = XboxFsMount(deviceName, &kProvider, context, 0, &context->mount);
    if (status != 0) { FATX_CloseVolume(context->volume); delete context; FATX_ModuleStop(); return status; }
    *mountHandle = context->mount;
    { std::lock_guard<std::mutex> lock(g_contextMutex); g_contexts.emplace(context->mount, context); }
    return STATUS_SUCCESS_VALUE;
}

extern "C" __declspec(dllexport) LONG __stdcall FATX_UnregisterFilesystemProvider(XBOX_FS_MOUNT mountHandle) {
    if (!mountHandle) return STATUS_INVALID_PARAMETER_VALUE;
    // The bridge rejects unmount while provider file handles remain open.
    const LONG status = XboxFsUnmount(mountHandle);
    if (status != 0) return status;
    FatxProviderContext* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        const auto it = g_contexts.find(mountHandle);
        if (it == g_contexts.end()) return STATUS_INVALID_PARAMETER_VALUE;
        context = it->second;
        g_contexts.erase(it);
    }
    FATX_CloseVolume(context->volume);
    delete context;
    FATX_ModuleStop();
    return STATUS_SUCCESS_VALUE;
}
