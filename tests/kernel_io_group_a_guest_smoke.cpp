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


#pragma pack(push, 4)
struct GuestFileObjectShareAccess {
    SHORT Type; UCHAR AccessFlags; UCHAR Flags;
    void* DeviceObject; void* FsContext; void* FsContext2;
    LONG FinalStatus; LARGE_INTEGER CurrentByteOffset;
    void* RelatedFileObject; void* CompletionContext; LONG LockCount;
    UCHAR LockEvent[32];
};
struct GuestShareAccess {
    UCHAR OpenCount, Readers, Writers, Deleters;
    UCHAR SharedRead, SharedWrite, SharedDelete;
};
#pragma pack(pop)
static_assert(sizeof(GuestFileObjectShareAccess) == 72, "FILE_OBJECT x86 size");
static_assert(sizeof(GuestShareAccess) == 7, "SHARE_ACCESS x86 size");
extern "C" LONG __stdcall K_IoCheckShareAccess(ACCESS_MASK, ULONG, void*, void*, BOOLEAN);
extern "C" void __stdcall K_IoSetShareAccess(ACCESS_MASK, ULONG, void*, void*);
extern "C" void __stdcall K_IoRemoveShareAccess(void*, void*);


#pragma pack(push, 4)
struct GuestDeviceQueue { SHORT Type; UCHAR Size; UCHAR Busy; GuestListEntry DeviceListHead; };
struct GuestEvent { UCHAR Type, Absolute, Size, Inserted; LONG SignalState; GuestListEntry WaitListHead; };
struct GuestDeviceObject {
    SHORT Type; USHORT Size; LONG ReferenceCount;
    void* DriverObject; GuestDeviceObject* MountedOrSelfDevice; void* CurrentIrp;
    ULONG Flags; void* DeviceExtension; UCHAR DeviceType, StartIoFlags;
    CHAR StackSize; BOOLEAN DeletePending; ULONG SectorSize;
    ULONG AlignmentRequirement; GuestDeviceQueue DeviceQueue;
    GuestEvent DeviceLock; ULONG StartIoKey;
};
struct GuestObjectStringDevice { USHORT Length, MaximumLength; char* Buffer; };
struct GuestDriverObject {
    void (__stdcall* DriverStartIo)(GuestDeviceObject*, void*);
    void (__stdcall* DriverDeleteDevice)(GuestDeviceObject*);
    LONG (__stdcall* DriverDismountVolume)(GuestDeviceObject*);
    void* MajorFunction[14];
};
#pragma pack(pop)
static_assert(sizeof(GuestDeviceObject) == 72, "DEVICE_OBJECT x86 size");
static_assert(sizeof(GuestDriverObject) == 68, "DRIVER_OBJECT x86 size");
extern "C" LONG __stdcall K_IoCreateDevice(void*, ULONG, void*, ULONG, BOOLEAN, void**);
extern "C" void __stdcall K_IoDeleteDevice(void*);

extern "C" void __stdcall K_IoStartPacket(void*, void*, ULONG*);
extern "C" void __stdcall K_IoStartNextPacket(void*);
extern "C" void __stdcall K_IoStartNextPacketByKey(void*, ULONG);
extern "C" LONG __stdcall K_IoDismountVolume(void*);
extern "C" LONG __stdcall K_IoDismountVolumeByName(void*);


extern "C" LONG __stdcall K_IoCreateFile(
    HANDLE*, ACCESS_MASK, GuestObjectAttributes*, GuestIoStatusBlock*,
    const LARGE_INTEGER*, ULONG, ULONG, ULONG, ULONG, ULONG);
extern "C" LONG __stdcall K_IoQueryFileInformation(
    void*, ULONG, ULONG, void*, ULONG*);
extern "C" LONG __stdcall K_IoQueryVolumeInformation(
    void*, ULONG, ULONG, void*, ULONG*);


namespace {
int Fail(int code, const char* message) { std::fprintf(stderr, "FAIL %d: %s\n", code, message); return code; }
volatile LONG g_deviceDeleteCalls = 0;
volatile LONG g_startIoCalls = 0;
volatile LONG g_dismountCalls = 0;
void* g_lastStartedIrp = nullptr;
void __stdcall StartIoCallback(GuestDeviceObject*, void* irp) {
    g_lastStartedIrp = irp;
    InterlockedIncrement(&g_startIoCalls);
}
LONG __stdcall DismountCallback(GuestDeviceObject*) {
    InterlockedIncrement(&g_dismountCalls);
    return 0;
}
void __stdcall DeleteDeviceCallback(GuestDeviceObject* device) {
    if (device && device->DeletePending && device->DeviceExtension)
        InterlockedIncrement(&g_deviceDeleteCalls);
}
}

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


    // Exact Xbox SHARE_ACCESS and FILE_OBJECT prefix layout: verify read/write
    // sharing conflicts, updates, removals, and reset semantics.
    GuestFileObjectShareAccess reader{}, writer{}, thirdReader{}, resetFile{};
    GuestShareAccess shares{};
    constexpr ACCESS_MASK readData = 0x1u;
    constexpr ACCESS_MASK writeData = 0x2u;
    constexpr ULONG shareReadWriteDelete = 0x7u;
    if (K_IoCheckShareAccess(readData, shareReadWriteDelete,
            &reader, &shares, TRUE) != 0 ||
        shares.OpenCount != 1 || shares.Readers != 1 ||
        shares.SharedRead != 1 || shares.SharedWrite != 1 ||
        shares.SharedDelete != 1)
        return Fail(15, "first shared read open");
    if (K_IoCheckShareAccess(writeData, 0x1u, &writer, &shares, TRUE) != 0 ||
        shares.OpenCount != 2 || shares.Writers != 1)
        return Fail(16, "second shared write open");
    if (K_IoCheckShareAccess(readData, 0, &thirdReader, &shares, TRUE) !=
        static_cast<LONG>(0xC0000043u))
        return Fail(17, "sharing violation");
    K_IoRemoveShareAccess(&writer, &shares);
    if (shares.OpenCount != 1 || shares.Writers != 0)
        return Fail(18, "remove writer share");
    if (K_IoCheckShareAccess(readData, 0x1u, &thirdReader, &shares, TRUE) != 0 ||
        shares.OpenCount != 2 || shares.Readers != 2)
        return Fail(19, "third reader open");
    K_IoRemoveShareAccess(&thirdReader, &shares);
    K_IoRemoveShareAccess(&reader, &shares);
    if (shares.OpenCount != 0 || shares.Readers != 0 ||
        shares.SharedRead != 0 || shares.SharedWrite != 0 ||
        shares.SharedDelete != 0)
        return Fail(20, "share counters empty");
    K_IoSetShareAccess(writeData, 0, &resetFile, &shares);
    if (shares.OpenCount != 1 || shares.Writers != 1 ||
        shares.SharedRead != 0 || shares.SharedWrite != 0)
        return Fail(21, "IoSetShareAccess");
    K_IoRemoveShareAccess(&resetFile, &shares);
    if (shares.OpenCount != 0 || shares.Writers != 0) return Fail(22, "remove reset share");


    GuestDriverObject driver{};
    driver.DriverStartIo = StartIoCallback;
    driver.DriverDeleteDevice = DeleteDeviceCallback;
    driver.DriverDismountVolume = DismountCallback;
    void* deviceRaw = nullptr;
    if (K_IoCreateDevice(&driver, 64, nullptr, 7, TRUE, &deviceRaw) != 0 ||
        !deviceRaw)
        return Fail(23, "IoCreateDevice");
    auto* device = static_cast<GuestDeviceObject*>(deviceRaw);
    if (device->Type != 3 || device->Size != sizeof(GuestDeviceObject) + 64 ||
        device->ReferenceCount != 1 || device->DriverObject != &driver ||
        device->MountedOrSelfDevice != device || (device->Flags & 0x8u) == 0 || (device->Flags & 0x80u) == 0 ||
        device->DeviceExtension != reinterpret_cast<UCHAR*>(device) + sizeof(*device) ||
        device->DeviceType != 7 || device->StackSize != 1 ||
        device->DeviceQueue.Type != 4 || device->DeviceQueue.Size != 12 ||
        device->DeviceQueue.Busy != FALSE ||
        device->DeviceLock.WaitListHead.Flink != &device->DeviceLock.WaitListHead)
        return Fail(24, "DEVICE_OBJECT layout");
    for (ULONG i = 0; i < 64; ++i)
        if (static_cast<UCHAR*>(device->DeviceExtension)[i] != 0) return Fail(25, "zeroed device extension");
    int irp1 = 1, irp2 = 2, irp3 = 3;
    ULONG key2 = 20, key3 = 30;
    K_IoStartPacket(device, &irp1, nullptr);
    K_IoStartPacket(device, &irp2, &key2);
    K_IoStartPacket(device, &irp3, &key3);
    if (g_startIoCalls != 1 || device->CurrentIrp != &irp1 || device->StartIoKey != 0) return Fail(27, "first StartIo");
    K_IoStartNextPacketByKey(device, key3);
    if (g_startIoCalls != 2 || g_lastStartedIrp != &irp3 ||
        device->CurrentIrp != &irp3 || device->StartIoKey != key3) return Fail(28, "keyed StartIo");
    K_IoStartNextPacket(device);
    if (g_startIoCalls != 3 || g_lastStartedIrp != &irp2 ||
        device->CurrentIrp != &irp2 || device->StartIoKey != key2) return Fail(29, "next StartIo");
    K_IoStartNextPacket(device);
    if (device->CurrentIrp != nullptr || device->DeviceQueue.Busy != FALSE || device->StartIoKey != 0)
        return Fail(30, "device queue idle");
    if (K_IoDismountVolume(device) != 0 || g_dismountCalls != 1)
        return Fail(31, "IoDismountVolume callback");
    K_IoDeleteDevice(device);
    if (g_deviceDeleteCalls != 1) return Fail(26, "IoDeleteDevice callback");


    char namedDeviceText[] = "\\Device\\XboxRuntimeNamed";
    GuestObjectStringDevice namedDeviceName{};
    namedDeviceName.Length = static_cast<USHORT>(std::strlen(namedDeviceText));
    namedDeviceName.MaximumLength = namedDeviceName.Length;
    namedDeviceName.Buffer = namedDeviceText;
    void* namedDevice = nullptr;
    if (K_IoCreateDevice(&driver, 16, &namedDeviceName, 7, FALSE,
                         &namedDevice) != 0 || !namedDevice)
        return Fail(32, "named IoCreateDevice");
    void* duplicateNamedDevice = nullptr;
    if (K_IoCreateDevice(&driver, 16, &namedDeviceName, 7, FALSE,
                         &duplicateNamedDevice) != static_cast<LONG>(0xC0000035u) ||
        duplicateNamedDevice != nullptr)
        return Fail(33, "named device collision");
    if (K_IoDismountVolumeByName(&namedDeviceName) != 0 ||
        g_dismountCalls != 2)
        return Fail(34, "IoDismountVolumeByName callback");
    K_IoDeleteDevice(namedDevice);
    if (g_deviceDeleteCalls != 2 ||
        K_IoDismountVolumeByName(&namedDeviceName) != static_cast<LONG>(0xC0000034u))
        return Fail(35, "named device cleanup");

    std::puts("kernel_io_group_a_guest_smoke: PASS (ANSI ABI, file create/open, file/volume queries)");
    return 0;
}
