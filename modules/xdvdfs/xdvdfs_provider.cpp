#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include "guest32/kernel_filesystem_bridge.h"
#include "xdvdfs.h"

namespace {
constexpr LONG STATUS_SUCCESS_VALUE = 0;
constexpr LONG STATUS_INVALID_PARAMETER_VALUE = static_cast<LONG>(0xC000000D);
constexpr LONG STATUS_NO_MEMORY_VALUE = static_cast<LONG>(0xC0000017);
constexpr LONG STATUS_ACCESS_DENIED_VALUE = static_cast<LONG>(0xC0000022);
constexpr LONG STATUS_OBJECT_NAME_NOT_FOUND_VALUE = static_cast<LONG>(0xC0000034);
constexpr LONG STATUS_FILE_IS_A_DIRECTORY_VALUE = static_cast<LONG>(0xC00000BA);
constexpr LONG STATUS_MEDIA_WRITE_PROTECTED_VALUE = static_cast<LONG>(0xC00000A2);
constexpr LONG STATUS_IO_DEVICE_ERROR_VALUE = static_cast<LONG>(0xC0000185);
constexpr ACCESS_MASK WRITE_MASK = 0x40000000u | 0x00000002u | 0x00000004u | 0x00010000u | 0x10000000u;

struct XdvdfsProviderContext { XBOX_FS_MOUNT mount = nullptr; };
struct XdvdfsProviderFile { std::string path; XDVDFS_FILE_INFO entry{}; };
std::mutex g_contextMutex;
std::unordered_map<XBOX_FS_MOUNT, XdvdfsProviderContext*> g_contexts;

bool ToUtf8(const WCHAR* wide, std::string& out) {
    if (!wide || wcslen(wide) > 240) return false;
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0 || bytes > 1024) return false;
    out.resize(static_cast<size_t>(bytes));
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, &out[0], bytes, nullptr, nullptr);
    if (written != bytes) return false;
    out.pop_back();
    std::replace(out.begin(), out.end(), '\\', '/');
    return true;
}
LONG __stdcall ProviderOpen(void*, const WCHAR* path, ACCESS_MASK access, ULONG, void** output) {
    if (output) *output = nullptr;
    if (!path || !output) return STATUS_INVALID_PARAMETER_VALUE;
    if (access & WRITE_MASK) return STATUS_MEDIA_WRITE_PROTECTED_VALUE;
    auto* file = new (std::nothrow) XdvdfsProviderFile();
    if (!file) return STATUS_NO_MEMORY_VALUE;
    if (!ToUtf8(path, file->path)) { delete file; return STATUS_INVALID_PARAMETER_VALUE; }
    const int result = XDVDFS_GetFileInfo(file->path.c_str(), &file->entry);
    if (result != 0) { delete file; return result == 3 ? STATUS_OBJECT_NAME_NOT_FOUND_VALUE : STATUS_IO_DEVICE_ERROR_VALUE; }
    if (file->entry.attributes & XDVDFS_ATTR_DIRECTORY) { delete file; return STATUS_FILE_IS_A_DIRECTORY_VALUE; }
    *output = file;
    return STATUS_SUCCESS_VALUE;
}
LONG __stdcall ProviderRead(void*, void* fileOpaque, ULONGLONG offset, void* buffer, ULONG length, ULONG* transferred) {
    auto* file = static_cast<XdvdfsProviderFile*>(fileOpaque);
    if (!file || !transferred || (!buffer && length)) return STATUS_INVALID_PARAMETER_VALUE;
    *transferred = 0;
    uint32_t bytesRead = 0;
    const int result = XDVDFS_ReadFile(file->path.c_str(), offset, buffer, length, &bytesRead);
    *transferred = static_cast<ULONG>(bytesRead);
    if (result == 0) return STATUS_SUCCESS_VALUE;
    if (result == 3) return STATUS_OBJECT_NAME_NOT_FOUND_VALUE;
    if (result == 4) return STATUS_FILE_IS_A_DIRECTORY_VALUE;
    return STATUS_IO_DEVICE_ERROR_VALUE;
}
LONG __stdcall ProviderWrite(void*, void*, ULONGLONG, const void*, ULONG, ULONG* transferred) {
    if (transferred) *transferred = 0;
    return STATUS_MEDIA_WRITE_PROTECTED_VALUE;
}
LONG __stdcall ProviderQuery(void*, void* fileOpaque, XBOX_FS_FILE_INFO* info) {
    auto* file = static_cast<XdvdfsProviderFile*>(fileOpaque);
    if (!file || !info) return STATUS_INVALID_PARAMETER_VALUE;
    XDVDFS_FILE_INFO current{};
    const int result = XDVDFS_GetFileInfo(file->path.c_str(), &current);
    if (result != 0) return result == 3 ? STATUS_OBJECT_NAME_NOT_FOUND_VALUE : STATUS_IO_DEVICE_ERROR_VALUE;
    info->size = current.size;
    info->allocationSize = current.size ? ((static_cast<ULONGLONG>(current.size) + XDVDFS_SECTOR_SIZE - 1) / XDVDFS_SECTOR_SIZE) * XDVDFS_SECTOR_SIZE : 0;
    DWORD attrs = 0;
    if (current.attributes & XDVDFS_ATTR_READONLY) attrs |= FILE_ATTRIBUTE_READONLY;
    if (current.attributes & XDVDFS_ATTR_HIDDEN) attrs |= FILE_ATTRIBUTE_HIDDEN;
    if (current.attributes & XDVDFS_ATTR_SYSTEM) attrs |= FILE_ATTRIBUTE_SYSTEM;
    if (current.attributes & XDVDFS_ATTR_DIRECTORY) attrs |= FILE_ATTRIBUTE_DIRECTORY;
    if (current.attributes & XDVDFS_ATTR_ARCHIVE) attrs |= FILE_ATTRIBUTE_ARCHIVE;
    info->attributes = attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
    info->reserved = 0;
    return STATUS_SUCCESS_VALUE;
}
void __stdcall ProviderClose(void*, void* fileOpaque) { delete static_cast<XdvdfsProviderFile*>(fileOpaque); }
const XBOX_FS_PROVIDER_V1 kProvider = {
    XBOX_FS_PROVIDER_ABI_V1, sizeof(XBOX_FS_PROVIDER_V1), L"XDVDFS",
    ProviderOpen, ProviderRead, ProviderWrite, ProviderQuery, ProviderClose
};
}

extern "C" __declspec(dllexport) LONG __stdcall XDVDFS_RegisterFilesystemProvider(
    const WCHAR* deviceName, const WCHAR* imagePath, XBOX_FS_MOUNT* mountHandle) {
    if (!deviceName || !*deviceName || !imagePath || !*imagePath || !mountHandle)
        return STATUS_INVALID_PARAMETER_VALUE;
    *mountHandle = nullptr;
    if (XDVDFS_ModuleStart() != 0) return STATUS_IO_DEVICE_ERROR_VALUE;
    XDVDFS_VOLUME_INFO volume{};
    if (XDVDFS_OpenImage(imagePath, &volume) != 0) { XDVDFS_ModuleStop(); return STATUS_IO_DEVICE_ERROR_VALUE; }
    auto* context = new (std::nothrow) XdvdfsProviderContext();
    if (!context) { XDVDFS_CloseImage(); XDVDFS_ModuleStop(); return STATUS_NO_MEMORY_VALUE; }
    const LONG status = XboxFsMount(deviceName, &kProvider, context,
        XBOX_FS_MOUNT_READ_ONLY, &context->mount);
    if (status != 0) { delete context; XDVDFS_CloseImage(); XDVDFS_ModuleStop(); return status; }
    *mountHandle = context->mount;
    { std::lock_guard<std::mutex> lock(g_contextMutex); g_contexts.emplace(context->mount, context); }
    return STATUS_SUCCESS_VALUE;
}

extern "C" __declspec(dllexport) LONG __stdcall XDVDFS_UnregisterFilesystemProvider(XBOX_FS_MOUNT mountHandle) {
    if (!mountHandle) return STATUS_INVALID_PARAMETER_VALUE;
    XdvdfsProviderContext* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        const auto it = g_contexts.find(mountHandle);
        if (it == g_contexts.end()) return STATUS_INVALID_PARAMETER_VALUE;
        context = it->second;
    }
    const LONG status = XboxFsUnmount(mountHandle);
    if (status != 0) return status;
    {
        std::lock_guard<std::mutex> lock(g_contextMutex);
        g_contexts.erase(mountHandle);
    }
    delete context;
    XDVDFS_CloseImage();
    XDVDFS_ModuleStop();
    return STATUS_SUCCESS_VALUE;
}
