#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include "xbox_storage.h"

static int Fail(const char* message) {
    std::fprintf(stderr, "xbox_storage_smoke failed: %s\n", message);
    return 1;
}
int main() {
    wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) ||
        !GetTempFileNameW(temp, L"xst", 0, path))
        return Fail("create temporary image path");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) { DeleteFileW(path); return Fail("create temporary image"); }
    LARGE_INTEGER size{}; size.QuadPart = 65536;
    const bool sized = SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && SetEndOfFile(file);
    CloseHandle(file);
    if (!sized) { DeleteFileW(path); return Fail("size temporary image"); }

    if (ModuleStart() != 0) { DeleteFileW(path); return Fail("ModuleStart"); }
    if (XboxStorageSetFatxImage(path, nullptr, 0, 0) != ERROR_SUCCESS) {
        ModuleStop(); DeleteFileW(path); return Fail("configure FATX image");
    }
    wchar_t value[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_IMAGE", value, MAX_PATH) ||
        std::wstring(value) != path ||
        !GetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_LENGTH", value, MAX_PATH) ||
        std::wcstoull(value, nullptr, 0) != 65536) {
        XboxStorageClearConfiguration(); ModuleStop(); DeleteFileW(path);
        return Fail("FATX environment configuration");
    }
    if (XboxStorageSetXdvdfsImage(path, nullptr) != ERROR_SUCCESS ||
        !GetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", value, MAX_PATH)) {
        XboxStorageClearConfiguration(); ModuleStop(); DeleteFileW(path);
        return Fail("configure XDVDFS image");
    }
    XboxStorageClearConfiguration();
    if (GetEnvironmentVariableW(L"XBOX_RUNTIME_FATX_IMAGE", value, MAX_PATH) ||
        GetEnvironmentVariableW(L"XBOX_RUNTIME_XDVDFS_IMAGE", value, MAX_PATH)) {
        ModuleStop(); DeleteFileW(path); return Fail("clear storage configuration");
    }
    ModuleStop();
    DeleteFileW(path);
    std::puts("PASS: storage module ABI, FATX/XDVDFS image configuration, derived image length, and cleanup");
    return 0;
}
