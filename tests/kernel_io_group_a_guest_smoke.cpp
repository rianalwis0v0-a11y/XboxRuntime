#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "kernel_thread_exports.h"

#pragma pack(push, 4)
struct GuestAnsiString { USHORT Length, MaximumLength; char* Buffer; };
struct GuestObjectAttributes { HANDLE RootDirectory; GuestAnsiString* ObjectName; ULONG Attributes; };
struct GuestIoStatusBlock { union { LONG Status; void* Pointer; }; ULONG Information; };
#pragma pack(pop)

struct GuestListEntry { GuestListEntry* Flink; GuestListEntry* Blink; };
struct GuestDispatcherHeader {
    UCHAR Type, Absolute, Size, Inserted;
    LONG SignalState;
    GuestListEntry WaitListHead;
};
struct GuestKQueue {
    GuestDispatcherHeader Header;
    GuestListEntry EntryListHead;
    ULONG CurrentCount;
    ULONG MaximumCount;
    GuestListEntry ThreadListHead;
};
extern "C" void __stdcall K_KeInitializeQueue(GuestKQueue*, ULONG);
extern "C" LONG __stdcall K_KeInsertQueue(GuestKQueue*, GuestListEntry*);
extern "C" GuestListEntry* __stdcall K_KeRundownQueue(GuestKQueue*);
extern "C" UCHAR __fastcall K_KfRaiseIrql(UCHAR);
extern "C" void __fastcall K_KfLowerIrql(UCHAR);
extern "C" DWORD __stdcall K_KeGetCurrentIrql();

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

    // Queue rundown returns a detached, null-terminated FIFO list and empties
    // the dispatcher queue.
    GuestKQueue queue{};
    K_KeInitializeQueue(&queue, 1);
    GuestListEntry first{}, second{};
    K_KeInsertQueue(&queue, &first);
    K_KeInsertQueue(&queue, &second);
    GuestListEntry* rundown = K_KeRundownQueue(&queue);
    if (rundown != &first || first.Flink != &second ||
        first.Blink != nullptr || second.Flink != nullptr ||
        K_KeRundownQueue(&queue) != nullptr)
        return 8;

    // KiUnlockDispatcherDatabase shares the exact lower-IRQL primitive.
    if (K_KfRaiseIrql(2) != 0 || K_KeGetCurrentIrql() != 2)
        return 9;
    K_KfLowerIrql(0);
    if (K_KeGetCurrentIrql() != 0) return 10;

    K_HalRequestSoftwareInterrupt(1);
    if (XboxKernelGetPendingSoftwareIrql() != 1) return 11;
    K_HalRequestSoftwareInterrupt(2);
    if (XboxKernelGetPendingSoftwareIrql() != 2) return 12;
    K_HalClearSoftwareInterrupt(1);
    if (XboxKernelGetPendingSoftwareIrql() != 2) return 13;
    K_HalClearSoftwareInterrupt(2);
    if (XboxKernelGetPendingSoftwareIrql() != 0) return 14;

    std::puts("kernel_io_group_a_guest_smoke: PASS (ANSI ABI, file create/open, file/volume queries)");
    return 0;
}
