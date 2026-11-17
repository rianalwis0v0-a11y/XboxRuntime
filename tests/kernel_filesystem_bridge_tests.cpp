#include "../guest32/kernel_filesystem_bridge.h"

#include <cassert>
#include <cstring>

namespace {
struct FakeFile { char bytes[8] = "kernel"; };
LONG __stdcall Open(void*, const WCHAR* path, ACCESS_MASK, ULONG, void** out) {
    if (!path || wcscmp(path, L"boot.bin") != 0) return static_cast<LONG>(0xC0000034u);
    *out = new FakeFile();
    return 0;
}
LONG __stdcall Read(void*, void* raw, ULONGLONG offset, void* buffer,
                    ULONG length, ULONG* transferred) {
    if (!raw || !transferred || (!buffer && length)) return static_cast<LONG>(0xC000000Du);
    auto* file = static_cast<FakeFile*>(raw);
    const ULONG size = 6;
    *transferred = offset >= size ? 0 : (length < size - static_cast<ULONG>(offset)
        ? length : size - static_cast<ULONG>(offset));
    if (*transferred) std::memcpy(buffer, file->bytes + offset, *transferred);
    return 0;
}
LONG __stdcall Write(void*, void* raw, ULONGLONG offset, const void* buffer,
                    ULONG length, ULONG* transferred) {
    if (!raw || !transferred || (!buffer && length) || offset + length > 7)
        return static_cast<LONG>(0xC000000Du);
    std::memcpy(static_cast<FakeFile*>(raw)->bytes + offset, buffer, length);
    *transferred = length;
    return 0;
}
LONG __stdcall Query(void*, void* raw, XBOX_FS_FILE_INFO* info) {
    if (!raw || !info) return static_cast<LONG>(0xC000000Du);
    info->size = 6; info->allocationSize = 8; info->attributes = 0x20; info->reserved = 0;
    return 0;
}
void __stdcall Close(void*, void* raw) { delete static_cast<FakeFile*>(raw); }
const XBOX_FS_PROVIDER_V1 provider = {
    XBOX_FS_PROVIDER_ABI_V1, sizeof(XBOX_FS_PROVIDER_V1), L"testfs",
    Open, Read, Write, Query, Close
};
}

int main() {
    XBOX_FS_MOUNT mount = nullptr;
    assert(XboxFsMount(L"\\Device\\Harddisk0\\Partition1", &provider,
        nullptr, 0, &mount) == 0);
    assert(mount != nullptr);

    XBOX_FS_FILE file = nullptr;
    assert(XboxFsOpen(L"\\device\\harddisk0\\partition1\\boot.bin",
        0x80000000u, 0, &file) == 0);
    assert(file != nullptr);

    // An open file pins its mount; unmount cannot invalidate live handles.
    assert(XboxFsUnmount(mount) == static_cast<LONG>(0x80000011u));

    char data[8]{};
    ULONG transferred = 0;
    assert(XboxFsRead(file, 0, data, 6, &transferred) == 0);
    assert(transferred == 6 && std::memcmp(data, "kernel", 6) == 0);

    XBOX_FS_FILE_INFO info{};
    assert(XboxFsQueryInformation(file, &info) == 0 && info.size == 6);
    const char replacement = 'K';
    assert(XboxFsWrite(file, 0, &replacement, 1, &transferred) == 0);
    assert(transferred == 1);
    XboxFsClose(file);
    assert(XboxFsUnmount(mount) == 0);

    XBOX_FS_MOUNT dvd = nullptr;
    assert(XboxFsMount(L"\\Device\\CdRom0", &provider, nullptr,
        XBOX_FS_MOUNT_READ_ONLY, &dvd) == 0);
    assert(XboxFsOpen(L"\\Device\\CdRom0\\boot.bin",
        0x40000000u, 0, &file) == static_cast<LONG>(0xC0000022u));
    assert(XboxFsOpen(L"\\Device\\CdRom0\\boot.bin",
        0x80000000u, 0, &file) == 0);
    assert(XboxFsWrite(file, 0, &replacement, 1, &transferred) ==
        static_cast<LONG>(0xC0000022u));
    XboxFsClose(file);
    assert(XboxFsUnmount(dvd) == 0);
    return 0;
}
