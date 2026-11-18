#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <string>
#include <vector>
#include "../modules/fatx/fatx_provider.h"
#include "../modules/xdvdfs/xdvdfs_provider.h"
#include "kernel_storage_mounts.h"

namespace {
struct MountRecord { XBOX_FS_MOUNT handle = nullptr; bool fatx = false; };
std::vector<MountRecord> g_mounts;

bool ReadEnvironment(const wchar_t* name, std::wstring& value) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (!required) return false;
    value.assign(required, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
    if (!copied || copied >= required) { value.clear(); return false; }
    value.resize(copied);
    return true;
}
bool ParseU64(const std::wstring& text, ULONGLONG& value) {
    if (text.empty() || text[0] == L'-') return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = wcstoull(text.c_str(), &end, 0);
    if (!end || *end) return false;
    value = static_cast<ULONGLONG>(parsed);
    return true;
}
bool FileLength(const std::wstring& path, ULONGLONG& length) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0;
    CloseHandle(file);
    if (!ok) return false;
    length = static_cast<ULONGLONG>(size.QuadPart);
    return true;
}
void RollbackMounts() {
    for (auto it = g_mounts.rbegin(); it != g_mounts.rend(); ++it) {
        if (it->fatx) FATX_UnregisterFilesystemProvider(it->handle);
        else XDVDFS_UnregisterFilesystemProvider(it->handle);
    }
    g_mounts.clear();
}
}

bool XboxKernelMountConfiguredStorage() {
    if (!g_mounts.empty()) return true;
    std::wstring image, device, offsetText, lengthText;
    if (ReadEnvironment(L"XBOX_RUNTIME_FATX_IMAGE", image)) {
        ULONGLONG offset = 0, length = 0, fileLength = 0;
        if (!ReadEnvironment(L"XBOX_RUNTIME_FATX_OFFSET", offsetText) &&
            GetLastError() != ERROR_ENVVAR_NOT_FOUND) return false;
        if (!offsetText.empty() && !ParseU64(offsetText, offset)) return false;
        if (!ReadEnvironment(L"XBOX_RUNTIME_FATX_LENGTH", lengthText) &&
            GetLastError() != ERROR_ENVVAR_NOT_FOUND) return false;
        if (!lengthText.empty()) {
            if (!ParseU64(lengthText, length)) return false;
        } else {
            if (!FileLength(image, fileLength) || offset > fileLength) return false;
            length = fileLength - offset;
        }
        if (!length || (FileLength(image, fileLength) &&
            (offset > fileLength || length > fileLength - offset))) return false;
        if (!ReadEnvironment(L"XBOX_RUNTIME_FATX_DEVICE", device))
            device = L"\\Device\\Harddisk0\\Partition1";
        XBOX_FS_MOUNT mount = nullptr;
        const LONG status = FATX_RegisterFilesystemProvider(
            device.c_str(), image.c_str(), offset, length, &mount);
        if (status != 0 || !mount) { RollbackMounts(); return false; }
        g_mounts.push_back({mount, true});
    }

    image.clear(); device.clear();
    if (ReadEnvironment(L"XBOX_RUNTIME_XDVDFS_IMAGE", image)) {
        if (!ReadEnvironment(L"XBOX_RUNTIME_XDVDFS_DEVICE", device))
            device = L"\\Device\\CdRom0";
        XBOX_FS_MOUNT mount = nullptr;
        const LONG status = XDVDFS_RegisterFilesystemProvider(
            device.c_str(), image.c_str(), &mount);
        if (status != 0 || !mount) { RollbackMounts(); return false; }
        g_mounts.push_back({mount, false});
    }
    return true;
}

void XboxKernelUnmountConfiguredStorage() {
    RollbackMounts();
}
