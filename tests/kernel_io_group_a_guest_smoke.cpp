#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "kernel_thread_exports.h"
#include "kernel_object_manager.h"
#include "kernel_object_reference.h"
#include "kernel_time.h"
#include "kernel_crypto.h"
#include "kernel_directory_objects.h"
#include "fatx_provider.h"
#include <vector>

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


extern "C" LONG __stdcall K_NtReadFile(HANDLE, HANDLE, void*, void*,
    GuestIoStatusBlock*, void*, ULONG, const LARGE_INTEGER*, ULONG*);
extern "C" LONG __stdcall K_NtWriteFile(HANDLE, HANDLE, void*, void*,
    GuestIoStatusBlock*, const void*, ULONG, const LARGE_INTEGER*, ULONG*);
extern "C" LONG __stdcall K_NtQueryInformationFile(HANDLE, GuestIoStatusBlock*,
    void*, ULONG, ULONG);
extern "C" LONG __stdcall K_NtClose(HANDLE);
extern "C" void KernelDispatch();
extern "C" __declspec(naked) void __stdcall DispatchXcSHAInit(std::uint8_t*) {
    __asm {
        push 335
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) void __stdcall DispatchXcSHAUpdate(std::uint8_t*, std::uint8_t*, std::uint32_t) {
    __asm {
        push 336
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) void __stdcall DispatchXcSHAFinal(std::uint8_t*, std::uint8_t*) {
    __asm {
        push 337
        jmp KernelDispatch
    }
}
extern "C" void __stdcall K_KeQuerySystemTimeNative(LARGE_INTEGER*);
extern "C" __declspec(naked) LONG __stdcall DispatchNtOpenDirectoryObject(HANDLE*, ULONG, void*) {
    __asm {
        push 201
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) LONG __stdcall DispatchNtQueryDirectoryObject(
    HANDLE, void*, ULONG, BOOLEAN, ULONG*, ULONG*) {
    __asm {
        push 208
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) LONG __stdcall DispatchNtSetSystemTime(LARGE_INTEGER*, LARGE_INTEGER*) {
    __asm {
        push 228
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) LONG __stdcall DispatchObReferenceObjectByHandle(HANDLE, void*, void**) {
    __asm {
        push 246
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) LONG __stdcall DispatchObReferenceObjectByPointer(void*, ULONG, void*, ULONG) {
    __asm {
        push 248
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) void __fastcall DispatchObfReferenceObject(void*) {
    __asm {
        push 251
        jmp KernelDispatch
    }
}
extern "C" __declspec(naked) void __fastcall DispatchObfDereferenceObject(void*) {
    __asm {
        push 250
        jmp KernelDispatch
    }
}

namespace {
int Fail(int code, const char* message);
void PutFatx16(std::vector<BYTE>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = BYTE(value); bytes[offset + 1] = BYTE(value >> 8);
}
void PutFatx32(std::vector<BYTE>& bytes, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = BYTE(value >> (i * 8));
}
bool WriteFatxFixture(const WCHAR* path) {
    std::vector<BYTE> image(64 * 1024, 0);
    std::memcpy(image.data(), "FATX", 4);
    PutFatx32(image, 8, 2); PutFatx32(image, 12, 1);
    PutFatx16(image, 0x1000 + 2, 0xFFFF);
    PutFatx16(image, 0x1000 + 4, 3);
    PutFatx16(image, 0x1000 + 6, 0xFFFF);
    const size_t data = 0x2000;
    BYTE* root = image.data() + data;
    root[0] = 8; root[1] = 0x20; std::memcpy(root + 2, "TEST.BIN", 8);
    PutFatx32(image, data + 0x2C, 2); PutFatx32(image, data + 0x30, 1500);
    root[64] = 0xFF;
    std::memset(image.data() + data + 1024, 'A', 1024);
    std::memset(image.data() + data + 2048, 'B', 1024);
    std::memcpy(image.data() + data + 1024, "fatxdata", 8);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, image.data(), static_cast<DWORD>(image.size()),
                              &written, nullptr) && written == image.size();
    CloseHandle(file);
    return ok;
}
int TestFilesystemNtDispatch() {
    WCHAR temp[MAX_PATH]{}, imagePath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp) ||
        !GetTempFileNameW(temp, L"ntf", 0, imagePath) ||
        !WriteFatxFixture(imagePath)) return Fail(40, "create FATX NT-dispatch fixture");
    XBOX_FS_MOUNT mount = nullptr;
    if (FATX_RegisterFilesystemProvider(L"\\Device\\Harddisk0\\Partition1",
            imagePath, 0, 64 * 1024, &mount) != 0 || !mount) {
        DeleteFileW(imagePath); return Fail(41, "register FATX NT-dispatch mount");
    }
    char devicePath[] = "\\Device\\Harddisk0\\Partition1\\TEST.BIN";
    GuestAnsiString name{};
    name.Length = static_cast<USHORT>(std::strlen(devicePath));
    name.MaximumLength = name.Length;
    name.Buffer = devicePath;
    GuestObjectAttributes attributes{}; attributes.ObjectName = &name;
    GuestIoStatusBlock io{};
    HANDLE handle = nullptr;
    LONG status = K_IoCreateFile(&handle, 0xC0000000u, &attributes, &io,
        nullptr, FILE_ATTRIBUTE_NORMAL, 7, 1, 0, 0);
    if (status != 0 || !handle || io.Status != 0) {
        FATX_UnregisterFilesystemProvider(mount); DeleteFileW(imagePath);
        return Fail(42, "NtCreateFile did not open mounted FATX path");
    }
    struct FileStandardInformation {
        LARGE_INTEGER AllocationSize, EndOfFile;
        ULONG NumberOfLinks; BOOLEAN DeletePending, Directory;
    } standard{};
    status = K_NtQueryInformationFile(handle, &io, &standard, sizeof(standard), 5);
    if (status != 0 || standard.EndOfFile.QuadPart != 1500 ||
        standard.AllocationSize.QuadPart != 2048) {
        K_NtClose(handle); FATX_UnregisterFilesystemProvider(mount);
        DeleteFileW(imagePath); return Fail(43, "NtQueryInformationFile provider data");
    }
    char data[8]{}; LARGE_INTEGER offset{}; ULONG key = 0;
    status = K_NtReadFile(handle, nullptr, nullptr, nullptr, &io,
                          data, sizeof(data), &offset, &key);
    if (status != 0 || io.Information != sizeof(data) ||
        std::memcmp(data, "fatxdata", 8) != 0) {
        K_NtClose(handle); FATX_UnregisterFilesystemProvider(mount);
        DeleteFileW(imagePath); return Fail(44, "NtReadFile provider dispatch");
    }
    offset.QuadPart = 0;
    status = K_NtWriteFile(handle, nullptr, nullptr, nullptr, &io,
                           "ntwrite!", 8, &offset, &key);
    if (status != 0 || io.Information != 8) {
        K_NtClose(handle); FATX_UnregisterFilesystemProvider(mount);
        DeleteFileW(imagePath); return Fail(45, "NtWriteFile provider dispatch");
    }
    std::memset(data, 0, sizeof(data)); offset.QuadPart = 0;
    status = K_NtReadFile(handle, nullptr, nullptr, nullptr, &io,
                          data, sizeof(data), &offset, &key);
    if (status != 0 || std::memcmp(data, "ntwrite!", 8) != 0) {
        K_NtClose(handle); FATX_UnregisterFilesystemProvider(mount);
        DeleteFileW(imagePath); return Fail(46, "NtWriteFile provider readback");
    }
    if (K_NtClose(handle) != 0 || FATX_UnregisterFilesystemProvider(mount) != 0) {
        DeleteFileW(imagePath); return Fail(47, "NtClose/provider unmount");
    }
    DeleteFileW(imagePath);
    return 0;
}
}

namespace {
int Fail(int code, const char* message) { std::fprintf(stderr, "FAIL %d: %s\n", code, message); return code; }
volatile LONG g_deviceDeleteCalls = 0;
volatile LONG g_startIoCalls = 0;
volatile LONG g_dismountCalls = 0;
volatile LONG g_objectDestroyed = 0;
void DestroyDispatchTestObject(void*) { InterlockedIncrement(&g_objectDestroyed); }
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


    K_HalDisableSystemInterrupt(5);
    if (XboxKernelIsSystemInterruptEnabled(5) != FALSE) return Fail(36, "disable system interrupt");
    K_HalEnableSystemInterrupt(5, 1);
    if (XboxKernelIsSystemInterruptEnabled(5) != TRUE ||
        XboxKernelGetSystemInterruptMode(5) != 1)
        return Fail(37, "enable system interrupt");
    K_HalDisableSystemInterrupt(5);
    if (XboxKernelIsSystemInterruptEnabled(5) != FALSE)
        return Fail(38, "mask system interrupt");

    // Named directory handles share object identity and survive closing the original handle.
    char directoryPath[] = "\\BaseNamedObjects\\XboxRuntimeDispatchSmoke";
    GuestAnsiString directoryName{};
    directoryName.Length = static_cast<USHORT>(std::strlen(directoryPath));
    directoryName.MaximumLength = directoryName.Length;
    directoryName.Buffer = directoryPath;
    GuestObjectAttributes directoryAttributes{};
    directoryAttributes.ObjectName = &directoryName;
    HANDLE createdDirectory = nullptr;
    if (K_NtCreateDirectoryObject(&createdDirectory, &directoryAttributes) != 0 || !createdDirectory)
        return Fail(56, "NtCreateDirectoryObject fixture");
    char childPath[] = "\\\\BaseNamedObjects\\\\XboxRuntimeDispatchSmoke\\\\Child";
    GuestAnsiString childName{};
    childName.Length = static_cast<USHORT>(std::strlen(childPath));
    childName.MaximumLength = childName.Length;
    childName.Buffer = childPath;
    GuestObjectAttributes childAttributes{};
    childAttributes.ObjectName = &childName;
    HANDLE childDirectory = nullptr;
    if (K_NtCreateDirectoryObject(&childDirectory, &childAttributes) != 0 ||
        !childDirectory)
        return Fail(64, "NtQueryDirectoryObject child fixture");
    alignas(4) unsigned char directoryBuffer[128]{};
    ULONG directoryContext = 0, directoryBytes = 0;
    if (DispatchNtQueryDirectoryObject(createdDirectory, directoryBuffer,
            sizeof(directoryBuffer), TRUE, &directoryContext, &directoryBytes) != 0)
        return Fail(65, "NtQueryDirectoryObject ordinal dispatch");
    auto* directoryEntry = reinterpret_cast<GuestAnsiString*>(directoryBuffer);
    if (directoryEntry->Length != 5 ||
        std::memcmp(directoryEntry->Buffer, "Child", 5) != 0 ||
        directoryContext != 1 || directoryBytes < sizeof(GuestAnsiString) + 5u + 1u)
        return Fail(66, "NtQueryDirectoryObject entry layout/context");
    if (!XboxKernelCloseObjectHandle(childDirectory))
        return Fail(67, "NtQueryDirectoryObject child cleanup");
    HANDLE openedDirectory = nullptr;
    if (DispatchNtOpenDirectoryObject(&openedDirectory, 0, &directoryAttributes) != 0 ||
        !openedDirectory || openedDirectory == createdDirectory)
        return Fail(57, "NtOpenDirectoryObject ordinal dispatch");
    if (!XboxKernelCloseObjectHandle(createdDirectory) ||
        !XboxKernelIsObjectHandle(openedDirectory) ||
        !XboxKernelCloseObjectHandle(openedDirectory))
        return Fail(58, "directory handle reference lifetime");
    openedDirectory = reinterpret_cast<HANDLE>(1);
    if (DispatchNtOpenDirectoryObject(&openedDirectory, 0, &directoryAttributes) !=
            static_cast<LONG>(0xC0000034u) || openedDirectory != nullptr)
        return Fail(59, "NtOpenDirectoryObject missing-name status");

    // NtCreateDirectoryObject must validate every guest pointer before reading it.
    if (K_NtCreateDirectoryObject(reinterpret_cast<HANDLE*>(1),
            &directoryAttributes) != static_cast<LONG>(0xC0000005u))
        return Fail(61, "NtCreateDirectoryObject invalid output pointer");
    HANDLE invalidDirectoryOutput = reinterpret_cast<HANDLE>(1);
    if (K_NtCreateDirectoryObject(&invalidDirectoryOutput,
            reinterpret_cast<void*>(1)) != static_cast<LONG>(0xC0000005u) ||
        invalidDirectoryOutput != nullptr)
        return Fail(62, "NtCreateDirectoryObject invalid attributes pointer/output");
    GuestAnsiString malformedDirectoryName = directoryName;
    malformedDirectoryName.Length = 8;
    malformedDirectoryName.MaximumLength = 4;
    GuestObjectAttributes malformedDirectoryAttributes{};
    malformedDirectoryAttributes.ObjectName = &malformedDirectoryName;
    HANDLE malformedDirectoryOutput = reinterpret_cast<HANDLE>(1);
    if (K_NtCreateDirectoryObject(&malformedDirectoryOutput,
            &malformedDirectoryAttributes) != static_cast<LONG>(0xC000000Du) ||
        malformedDirectoryOutput != nullptr)
        return Fail(63, "NtCreateDirectoryObject rejects malformed string lengths");

    // NtSetSystemTime must alter only the title clock, never the host Windows clock.
    XboxKernelResetSystemTime();
    LARGE_INTEGER guestTimeBefore{}, requestedGuestTime{}, previousGuestTime{}, guestTimeAfter{};
    K_KeQuerySystemTimeNative(&guestTimeBefore);
    requestedGuestTime.QuadPart = guestTimeBefore.QuadPart + 300000000LL;
    if (DispatchNtSetSystemTime(&requestedGuestTime, &previousGuestTime) != 0)
        return Fail(53, "NtSetSystemTime ordinal dispatch");
    K_KeQuerySystemTimeNative(&guestTimeAfter);
    if (previousGuestTime.QuadPart < guestTimeBefore.QuadPart - 10000000LL ||
        guestTimeAfter.QuadPart < requestedGuestTime.QuadPart ||
        guestTimeAfter.QuadPart > requestedGuestTime.QuadPart + 10000000LL)
        return Fail(54, "NtSetSystemTime virtual clock values");
    requestedGuestTime.QuadPart = -1;
    if (DispatchNtSetSystemTime(&requestedGuestTime, nullptr) != static_cast<LONG>(0xC000000Du))
        return Fail(55, "NtSetSystemTime rejects negative absolute time");
    XboxKernelResetSystemTime();

    // Exercise the four object-reference ordinals through the real import dispatcher.
    int objectPayload = 0;
    HANDLE objectHandle = XboxKernelCreateObjectHandle(
        &objectPayload, XboxKernelObjectType::Event, DestroyDispatchTestObject);
    if (!objectHandle) return Fail(48, "create object-reference dispatch fixture");
    void* referencedObject = nullptr;
    if (DispatchObReferenceObjectByHandle(objectHandle, nullptr, &referencedObject) != 0 ||
        referencedObject != &objectPayload)
        return Fail(49, "ObReferenceObjectByHandle dispatch");
    DispatchObfReferenceObject(referencedObject);
    DispatchObfDereferenceObject(referencedObject);
    DispatchObfDereferenceObject(referencedObject);
    if (DispatchObReferenceObjectByPointer(referencedObject, 0, nullptr, 0) != 0)
        return Fail(50, "ObReferenceObjectByPointer dispatch");
    DispatchObfDereferenceObject(referencedObject);
    referencedObject = reinterpret_cast<void*>(1);
    if (DispatchObReferenceObjectByHandle(reinterpret_cast<HANDLE>(1), nullptr,
            &referencedObject) != static_cast<LONG>(0xC0000008u) || referencedObject != nullptr)
        return Fail(51, "invalid object handle status/output");
    if (!XboxKernelCloseObjectHandle(objectHandle) || g_objectDestroyed != 1)
        return Fail(52, "object reference lifetime/close");

    // SHA-1 is exercised through the same ordinal thunk path used by XBE imports.
    std::uint8_t shaContext[116]{};
    std::uint8_t shaDigest[20]{};
    std::uint8_t shaInput[] = {'a', 'b', 'c'};
    DispatchXcSHAInit(shaContext);
    DispatchXcSHAUpdate(shaContext, shaInput, sizeof(shaInput));
    DispatchXcSHAFinal(shaContext, shaDigest);
    const std::uint8_t expectedSha[20] = {
        0xA9,0x99,0x3E,0x36,0x47,0x06,0x81,0x6A,0xBA,0x3E,
        0x25,0x71,0x78,0x50,0xC2,0x6C,0x9C,0xD0,0xD8,0x9D
    };
    if (std::memcmp(shaDigest, expectedSha, sizeof(expectedSha)) != 0)
        return Fail(60, "XcSHAInit/Update/Final ordinal dispatch");

    const int filesystemStatus = TestFilesystemNtDispatch();
    if (filesystemStatus) return filesystemStatus;

    std::puts("kernel_io_group_a_guest_smoke: PASS (ANSI ABI, file create/open, file/volume queries, FATX Nt* dispatch)");
    return 0;
}
