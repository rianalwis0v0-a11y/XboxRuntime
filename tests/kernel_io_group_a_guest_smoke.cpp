#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma pack(push, 4)
struct GuestAnsiString { USHORT Length, MaximumLength; char* Buffer; };
struct GuestObjectAttributes { HANDLE RootDirectory; GuestAnsiString* ObjectName; ULONG Attributes; };
struct GuestIoStatusBlock { union { LONG Status; void* Pointer; }; ULONG Information; };
#pragma pack(pop)
extern "C" LONG __stdcall K_IoCreateFile(
    HANDLE*, ACCESS_MASK, GuestObjectAttributes*, GuestIoStatusBlock*,
    const LARGE_INTEGER*, ULONG, ULONG, ULONG, ULONG, ULONG);
extern "C" LONG __stdcall K_IoQueryFileInformation(
    void*, ULONG, ULONG, void*, ULONG*);
extern "C" LONG __stdcall K_IoQueryVolumeInformation(
    void*, ULONG, ULONG, void*, ULONG*);

int main() {
    static_assert(sizeof(void*) == 4, "Xbox I/O smoke test requires x86");
    char directory[MAX_PATH]{};
    if (!GetTempPathA(MAX_PATH, directory)) return 1;
    char path[MAX_PATH]{};
    if (!GetTempFileNameA(directory, "xkr", 0, path)) return 2;
    HANDLE seed = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (seed == INVALID_HANDLE_VALUE) return 3;
    const char contents[] = "XboxRuntime IoCreateFile";
    DWORD written = 0;
    if (!WriteFile(seed, contents, sizeof(contents), &written, nullptr) ||
        written != sizeof(contents)) return 4;
    CloseHandle(seed);

    GuestAnsiString name{};
    name.Length = static_cast<USHORT>(std::strlen(path));
    name.MaximumLength = name.Length;
    name.Buffer = path;
    GuestObjectAttributes attributes{};
    attributes.ObjectName = &name;
    GuestIoStatusBlock io{};
    HANDLE file = nullptr;
    LONG status = K_IoCreateFile(&file, 0x80000000u, &attributes, &io,
        nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, 1, 0, 0);
    if (status != 0 || !file || io.Status != 0) {
        std::fprintf(stderr, "IoCreateFile failed: 0x%08lX\n",
                     static_cast<unsigned long>(status));
        DeleteFileA(path);
        return 5;
    }

    struct FileStandardInformation {
        LARGE_INTEGER AllocationSize;
        LARGE_INTEGER EndOfFile;
        ULONG NumberOfLinks;
        BOOLEAN DeletePending;
        BOOLEAN Directory;
    } standard{};
    ULONG returned = 0;
    status = K_IoQueryFileInformation(file, 5, sizeof(standard),
                                      &standard, &returned);
    if (status != 0 || returned != sizeof(standard) ||
        standard.EndOfFile.QuadPart != sizeof(contents)) {
        std::fprintf(stderr, "IoQueryFileInformation failed: 0x%08lX\n",
                     static_cast<unsigned long>(status));
        CloseHandle(file);
        DeleteFileA(path);
        return 6;
    }

    unsigned char volumeInfo[512]{};
    returned = 0;
    status = K_IoQueryVolumeInformation(file, 1, sizeof(volumeInfo),
                                        volumeInfo, &returned);
    if (status != 0 || returned == 0) {
        std::fprintf(stderr, "IoQueryVolumeInformation failed: 0x%08lX\n",
                     static_cast<unsigned long>(status));
        CloseHandle(file);
        DeleteFileA(path);
        return 7;
    }

    CloseHandle(file);
    DeleteFileA(path);
    std::puts("kernel_io_group_a_guest_smoke: PASS (ANSI ABI, file create/open, file/volume queries)");
    return 0;
}
