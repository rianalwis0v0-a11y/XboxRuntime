#include "kernel_filesystem_bridge.h"

#include <algorithm>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr LONG kSuccess = 0;
constexpr LONG kInvalidParameter = static_cast<LONG>(0xC000000Du);
constexpr LONG kNotSupported = static_cast<LONG>(0xC00000BBu);
constexpr LONG kNameNotFound = static_cast<LONG>(0xC0000034u);
constexpr LONG kAccessDenied = static_cast<LONG>(0xC0000022u);
constexpr LONG kDeviceBusy = static_cast<LONG>(0x80000011u);
constexpr ULONG kReadOnly = XBOX_FS_MOUNT_READ_ONLY;
constexpr ACCESS_MASK kWriteMask = 0x40000000u | 0x00000002u | 0x00000004u |
                                   0x00010000u | 0x10000000u;

struct Mount {
    static constexpr ULONGLONG kMagic = 0x58424F5846534D54ull;
    ULONGLONG magic = kMagic;
    std::wstring deviceName;
    const XBOX_FS_PROVIDER_V1* provider = nullptr;
    void* context = nullptr;
    DWORD flags = 0;
    ULONG openFiles = 0;
    bool active = true;
};
struct File {
    static constexpr ULONGLONG kMagic = 0x58424F584653464Cull;
    ULONGLONG magic = kMagic;
    Mount* mount = nullptr;
    void* providerFile = nullptr;
    std::mutex mutex;
    bool closed = false;
};
std::mutex g_mutex;
std::vector<Mount*> g_mounts;

bool EqualPrefix(const std::wstring& path, const std::wstring& prefix) {
    if (path.size() < prefix.size()) return false;
    if (CompareStringOrdinal(path.data(), static_cast<int>(prefix.size()),
            prefix.data(), static_cast<int>(prefix.size()), TRUE) != CSTR_EQUAL)
        return false;
    return path.size() == prefix.size() || path[prefix.size()] == L'\\';
}
bool ValidProvider(const XBOX_FS_PROVIDER_V1* p) {
    return p && p->abiVersion == XBOX_FS_PROVIDER_ABI_V1 &&
        p->structureSize >= sizeof(XBOX_FS_PROVIDER_V1) &&
        p->filesystemName && p->Open && p->Read && p->QueryInformation &&
        p->Close;
}
}

extern "C" LONG __stdcall XboxFsMount(const WCHAR* deviceName,
    const XBOX_FS_PROVIDER_V1* provider, void* providerContext,
    DWORD mountFlags, XBOX_FS_MOUNT* mountHandle) {
    if (!deviceName || !*deviceName || !mountHandle ||
        !ValidProvider(provider) || (mountFlags & ~kReadOnly))
        return kInvalidParameter;
    *mountHandle = nullptr;
    std::wstring name(deviceName);
    if (name.back() == L'\\') name.pop_back();
    if (name.empty() || name.front() != L'\\') return kInvalidParameter;
    auto* mount = new (std::nothrow) Mount();
    if (!mount) return static_cast<LONG>(0xC0000017u);
    mount->deviceName = std::move(name);
    mount->provider = provider;
    mount->context = providerContext;
    mount->flags = mountFlags;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const Mount* existing : g_mounts) {
            if (existing->active && CompareStringOrdinal(
                existing->deviceName.c_str(), -1, mount->deviceName.c_str(), -1,
                TRUE) == CSTR_EQUAL) {
                delete mount;
                return static_cast<LONG>(0xC0000035u);
            }
        }
        g_mounts.push_back(mount);
    }
    *mountHandle = mount;
    return kSuccess;
}

extern "C" LONG __stdcall XboxFsUnmount(XBOX_FS_MOUNT handle) {
    auto* mount = static_cast<Mount*>(handle);
    if (!mount) return kInvalidParameter;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = std::find(g_mounts.begin(), g_mounts.end(), mount);
    if (it == g_mounts.end() || !mount->active || mount->magic != Mount::kMagic)
        return kInvalidParameter;
    if (mount->openFiles) return kDeviceBusy;
    mount->active = false;
    mount->magic = 0;
    g_mounts.erase(it);
    delete mount;
    return kSuccess;
}

extern "C" LONG __stdcall XboxFsOpen(const WCHAR* absolutePath,
    ACCESS_MASK desiredAccess, ULONG shareAccess, XBOX_FS_FILE* fileHandle) {
    if (!absolutePath || !*absolutePath || !fileHandle) return kInvalidParameter;
    *fileHandle = nullptr;
    Mount* selected = nullptr;
    std::wstring relative;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        size_t best = 0;
        for (Mount* mount : g_mounts) {
            if (mount->active && mount->deviceName.size() >= best &&
                EqualPrefix(absolutePath, mount->deviceName)) {
                selected = mount;
                best = mount->deviceName.size();
            }
        }
        if (!selected) return kNameNotFound;
        if ((selected->flags & kReadOnly) && (desiredAccess & kWriteMask))
            return kAccessDenied;
        const WCHAR* tail = absolutePath + best;
        while (*tail == L'\\') ++tail;
        relative.assign(tail);
        ++selected->openFiles;
    }
    void* providerFile = nullptr;
    LONG status = selected->provider->Open(selected->context,
        relative.c_str(), desiredAccess, shareAccess, &providerFile);
    if (status != kSuccess || !providerFile) {
        std::lock_guard<std::mutex> lock(g_mutex);
        --selected->openFiles;
        return status == kSuccess ? kInvalidParameter : status;
    }
    auto* file = new (std::nothrow) File();
    if (!file) {
        selected->provider->Close(selected->context, providerFile);
        std::lock_guard<std::mutex> lock(g_mutex);
        --selected->openFiles;
        return static_cast<LONG>(0xC0000017u);
    }
    file->mount = selected;
    file->providerFile = providerFile;
    *fileHandle = file;
    return kSuccess;
}

extern "C" LONG __stdcall XboxFsRead(XBOX_FS_FILE handle, ULONGLONG offset,
    void* buffer, ULONG length, ULONG* transferred) {
    auto* file = static_cast<File*>(handle);
    if (!file || file->magic != File::kMagic || !transferred ||
        (!buffer && length)) return kInvalidParameter;
    *transferred = 0;
    std::lock_guard<std::mutex> lock(file->mutex);
    if (file->closed) return kInvalidParameter;
    return file->mount->provider->Read(file->mount->context,
        file->providerFile, offset, buffer, length, transferred);
}

extern "C" LONG __stdcall XboxFsWrite(XBOX_FS_FILE handle, ULONGLONG offset,
    const void* buffer, ULONG length, ULONG* transferred) {
    auto* file = static_cast<File*>(handle);
    if (!file || file->magic != File::kMagic || !transferred ||
        (!buffer && length)) return kInvalidParameter;
    *transferred = 0;
    std::lock_guard<std::mutex> lock(file->mutex);
    if (file->closed) return kInvalidParameter;
    if (file->mount->flags & kReadOnly) return kAccessDenied;
    if (!file->mount->provider->Write) return kNotSupported;
    return file->mount->provider->Write(file->mount->context,
        file->providerFile, offset, buffer, length, transferred);
}

extern "C" LONG __stdcall XboxFsQueryInformation(XBOX_FS_FILE handle,
    XBOX_FS_FILE_INFO* information) {
    auto* file = static_cast<File*>(handle);
    if (!file || file->magic != File::kMagic || !information)
        return kInvalidParameter;
    std::lock_guard<std::mutex> lock(file->mutex);
    if (file->closed) return kInvalidParameter;
    return file->mount->provider->QueryInformation(file->mount->context,
        file->providerFile, information);
}

extern "C" void __stdcall XboxFsClose(XBOX_FS_FILE handle) {
    auto* file = static_cast<File*>(handle);
    if (!file || file->magic != File::kMagic) return;
    {
        std::lock_guard<std::mutex> lock(file->mutex);
        if (file->closed) return;
        file->closed = true;
        file->mount->provider->Close(file->mount->context, file->providerFile);
        file->magic = 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (file->mount->openFiles) --file->mount->openFiles;
    }
    delete file;
}
