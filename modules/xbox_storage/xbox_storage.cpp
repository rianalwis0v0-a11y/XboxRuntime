#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", nullptr);
    SetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_DEVICE", nullptr);
}

extern "C" int __cdecl ModuleStart(void) {
    OutputDebugStringW(L"[XboxRuntime:Storage] Explicit image-backed storage configuration ready.\n");
    return ERROR_SUCCESS;
}
extern "C" void __cdecl ModuleStop(void) {}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
