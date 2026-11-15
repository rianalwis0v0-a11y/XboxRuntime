#include "kernel_io_exports.h"

#include <mutex>
#include <deque>

#pragma pack(push, 4)
struct GuestFileObjectShareAccess {
    SHORT Type;
    UCHAR AccessFlags;
    UCHAR Flags;
    void* DeviceObject;
    void* FsContext;
    void* FsContext2;
    LONG FinalStatus;
    LARGE_INTEGER CurrentByteOffset;
    void* RelatedFileObject;
    void* CompletionContext;
    LONG LockCount;
    UCHAR LockEvent[32];
};
struct GuestShareAccess {
    UCHAR OpenCount, Readers, Writers, Deleters;
    UCHAR SharedRead, SharedWrite, SharedDelete;
};
#pragma pack(pop)
static_assert(offsetof(GuestFileObjectShareAccess, DeviceObject) == 4, "FILE_OBJECT DeviceObject offset");
static_assert(offsetof(GuestFileObjectShareAccess, FsContext) == 8, "FILE_OBJECT FsContext offset");
static_assert(offsetof(GuestFileObjectShareAccess, FinalStatus) == 16, "FILE_OBJECT FinalStatus offset");
static_assert(offsetof(GuestFileObjectShareAccess, CurrentByteOffset) == 20, "FILE_OBJECT CurrentByteOffset offset");
static_assert(offsetof(GuestFileObjectShareAccess, RelatedFileObject) == 28, "FILE_OBJECT RelatedFileObject offset");
static_assert(offsetof(GuestFileObjectShareAccess, LockCount) == 36, "FILE_OBJECT LockCount offset");
static_assert(sizeof(GuestFileObjectShareAccess) == 72, "FILE_OBJECT x86 size");
static_assert(sizeof(GuestShareAccess) == 7, "SHARE_ACCESS x86 size");

namespace {
constexpr LONG kStatusNotSupported = static_cast<LONG>(0xC00000BBu);
[[noreturn]] void UnsupportedIoVoidExport(ULONG ordinal) {
    ULONG_PTR parameter = ordinal;
    RaiseException(0xC000001Du, EXCEPTION_NONCONTINUABLE, 1, &parameter);
}
}
extern "C" void* __stdcall K_IoAllocateIrp(CHAR) { return nullptr; }
extern "C" void* __stdcall K_IoBuildAsynchronousFsdRequest(
    ULONG, void*, void*, ULONG, const LARGE_INTEGER*, void*) { return nullptr; }
extern "C" void* __stdcall K_IoBuildDeviceIoControlRequest(
    ULONG, void*, void*, ULONG, void*, ULONG, BOOLEAN, void*, void*) {
    return nullptr;
}
extern "C" void* __stdcall K_IoBuildSynchronousFsdRequest(
    ULONG, void*, void*, ULONG, const LARGE_INTEGER*, void*, void*) {
    return nullptr;
}

constexpr UCHAR kFileObjectReadAccess = 1u << 1;
constexpr UCHAR kFileObjectWriteAccess = 1u << 2;
constexpr UCHAR kFileObjectDeleteAccess = 1u << 3;
constexpr UCHAR kFileObjectSharedRead = 1u << 4;
constexpr UCHAR kFileObjectSharedWrite = 1u << 5;
constexpr UCHAR kFileObjectSharedDelete = 1u << 6;
constexpr ACCESS_MASK kDeleteAccess = 0x00010000u;
constexpr ACCESS_MASK kGenericRead = 0x80000000u;
constexpr ACCESS_MASK kGenericWrite = 0x40000000u;
constexpr ACCESS_MASK kGenericExecute = 0x20000000u;
constexpr ACCESS_MASK kGenericAll = 0x10000000u;
struct ShareRequest {
    bool read;
    bool write;
    bool remove;
    bool sharedRead;
    bool sharedWrite;
    bool sharedDelete;
};
ShareRequest MakeShareRequest(ACCESS_MASK access, ULONG share) {
    return {
        (access & (0x00000001u | 0x00000020u | kGenericRead | kGenericExecute | kGenericAll)) != 0,
        (access & (0x00000002u | 0x00000004u | kGenericWrite | kGenericAll)) != 0,
        (access & (kDeleteAccess | kGenericAll)) != 0,
        (share & 1u) != 0, (share & 2u) != 0, (share & 4u) != 0
    };
}
bool ShareCountsCanOpen(const GuestShareAccess& counts,
                        const ShareRequest& request) {
    if (request.read && counts.SharedRead != counts.OpenCount) return false;
    if (request.write && counts.SharedWrite != counts.OpenCount) return false;
    if (request.remove && counts.SharedDelete != counts.OpenCount) return false;
    if (!request.sharedRead && counts.Readers != 0) return false;
    if (!request.sharedWrite && counts.Writers != 0) return false;
    if (!request.sharedDelete && counts.Deleters != 0) return false;
    return counts.OpenCount != 0xFF;
}
void ApplyShareOpen(GuestFileObjectShareAccess& file,
                    GuestShareAccess& counts, const ShareRequest& request) {
    file.AccessFlags &= UCHAR(~(kFileObjectReadAccess | kFileObjectWriteAccess |
        kFileObjectDeleteAccess | kFileObjectSharedRead |
        kFileObjectSharedWrite | kFileObjectSharedDelete));
    if (request.read) file.AccessFlags |= kFileObjectReadAccess;
    if (request.write) file.AccessFlags |= kFileObjectWriteAccess;
    if (request.remove) file.AccessFlags |= kFileObjectDeleteAccess;
    if (request.sharedRead) file.AccessFlags |= kFileObjectSharedRead;
    if (request.sharedWrite) file.AccessFlags |= kFileObjectSharedWrite;
    if (request.sharedDelete) file.AccessFlags |= kFileObjectSharedDelete;
    ++counts.OpenCount;
    if (request.read) ++counts.Readers;
    if (request.write) ++counts.Writers;
    if (request.remove) ++counts.Deleters;
    if (request.sharedRead) ++counts.SharedRead;
    if (request.sharedWrite) ++counts.SharedWrite;
    if (request.sharedDelete) ++counts.SharedDelete;
}
std::mutex g_shareAccessMutex;

extern "C" LONG __stdcall K_IoCheckShareAccess(
    ACCESS_MASK desiredAccess, ULONG desiredShareAccess,
    void* rawFileObject, void* rawShareAccess, BOOLEAN update) {
    if (!rawFileObject || !rawShareAccess) return LONG(0xC000000Du);
    auto* file = static_cast<GuestFileObjectShareAccess*>(rawFileObject);
    auto* counts = static_cast<GuestShareAccess*>(rawShareAccess);
    const ShareRequest request = MakeShareRequest(desiredAccess, desiredShareAccess);
    std::lock_guard<std::mutex> lock(g_shareAccessMutex);
    if (!ShareCountsCanOpen(*counts, request))
        return LONG(0xC0000043u); // STATUS_SHARING_VIOLATION
    if (update) ApplyShareOpen(*file, *counts, request);
    return 0;
}
extern "C" void __stdcall K_IoSetShareAccess(
    ACCESS_MASK desiredAccess, ULONG desiredShareAccess,
    void* rawFileObject, void* rawShareAccess) {
    if (!rawFileObject || !rawShareAccess) return;
    auto* file = static_cast<GuestFileObjectShareAccess*>(rawFileObject);
    auto* counts = static_cast<GuestShareAccess*>(rawShareAccess);
    const ShareRequest request = MakeShareRequest(desiredAccess, desiredShareAccess);
    std::lock_guard<std::mutex> lock(g_shareAccessMutex);
    counts->OpenCount = counts->Readers = counts->Writers = counts->Deleters = 0;
    counts->SharedRead = counts->SharedWrite = counts->SharedDelete = 0;
    ApplyShareOpen(*file, *counts, request);
}
extern "C" void __stdcall K_IoRemoveShareAccess(
    void* rawFileObject, void* rawShareAccess) {
    if (!rawFileObject || !rawShareAccess) return;
    auto* file = static_cast<GuestFileObjectShareAccess*>(rawFileObject);
    auto* counts = static_cast<GuestShareAccess*>(rawShareAccess);
    std::lock_guard<std::mutex> lock(g_shareAccessMutex);
    if (counts->OpenCount) --counts->OpenCount;
    if ((file->AccessFlags & kFileObjectReadAccess) && counts->Readers) --counts->Readers;
    if ((file->AccessFlags & kFileObjectWriteAccess) && counts->Writers) --counts->Writers;
    if ((file->AccessFlags & kFileObjectDeleteAccess) && counts->Deleters) --counts->Deleters;
    if ((file->AccessFlags & kFileObjectSharedRead) && counts->SharedRead) --counts->SharedRead;
    if ((file->AccessFlags & kFileObjectSharedWrite) && counts->SharedWrite) --counts->SharedWrite;
    if ((file->AccessFlags & kFileObjectSharedDelete) && counts->SharedDelete) --counts->SharedDelete;
}


#pragma pack(push, 4)
struct GuestListEntryDevice { GuestListEntryDevice* Flink; GuestListEntryDevice* Blink; };
struct GuestDeviceQueue {
    SHORT Type; UCHAR Size; UCHAR Busy; GuestListEntryDevice DeviceListHead;
};
struct GuestEvent {
    UCHAR Type, Absolute, Size, Inserted;
    LONG SignalState;
    GuestListEntryDevice WaitListHead;
};
struct GuestDeviceObject {
    SHORT Type; USHORT Size; LONG ReferenceCount;
    void* DriverObject; GuestDeviceObject* MountedOrSelfDevice;
    void* CurrentIrp; ULONG Flags; void* DeviceExtension;
    UCHAR DeviceType, StartIoFlags; CHAR StackSize; BOOLEAN DeletePending;
    ULONG SectorSize, AlignmentRequirement;
    GuestDeviceQueue DeviceQueue; GuestEvent DeviceLock; ULONG StartIoKey;
};
struct GuestDriverObject {
    void* DriverStartIo;
    void (__stdcall* DriverDeleteDevice)(GuestDeviceObject*);
    LONG (__stdcall* DriverDismountVolume)(GuestDeviceObject*);
    void* MajorFunction[14];
};
struct GuestObjectStringDevice { USHORT Length, MaximumLength; char* Buffer; };
#pragma pack(pop)
static_assert(sizeof(GuestDeviceQueue) == 12, "KDEVICE_QUEUE x86 size");
static_assert(sizeof(GuestEvent) == 16, "KEVENT x86 size");
static_assert(offsetof(GuestDeviceObject, DriverObject) == 8, "DEVICE_OBJECT DriverObject offset");
static_assert(offsetof(GuestDeviceObject, DeviceExtension) == 24, "DEVICE_OBJECT DeviceExtension offset");
static_assert(offsetof(GuestDeviceObject, DeviceQueue) == 40, "DEVICE_OBJECT DeviceQueue offset");
static_assert(offsetof(GuestDeviceObject, DeviceLock) == 52, "DEVICE_OBJECT DeviceLock offset");
static_assert(offsetof(GuestDeviceObject, StartIoKey) == 68, "DEVICE_OBJECT StartIoKey offset");
static_assert(sizeof(GuestDeviceObject) == 72, "DEVICE_OBJECT x86 size");
static_assert(sizeof(GuestDriverObject) == 68, "DRIVER_OBJECT x86 size");
static_assert(sizeof(GuestObjectStringDevice) == 8, "OBJECT_STRING x86 size");

namespace {
std::mutex g_deviceObjectMutex;
std::unordered_map<GuestDeviceObject*, SIZE_T> g_deviceObjects;
struct QueuedDeviceIrp { void* irp; ULONG key; };
std::unordered_map<GuestDeviceObject*, std::deque<QueuedDeviceIrp>> g_devicePacketQueues;
void InitializeDeviceList(GuestListEntryDevice* head) {
    head->Flink = head;
    head->Blink = head;
}
void InitializeDeviceObject(GuestDeviceObject* device, SIZE_T totalSize,
                            void* driver, ULONG deviceType,
                            ULONG extensionSize, BOOLEAN exclusive) {
    device->Type = 3; // IO_TYPE_DEVICE
    device->Size = static_cast<USHORT>(totalSize);
    device->ReferenceCount = 1;
    device->DriverObject = driver;
    device->MountedOrSelfDevice = device;
    device->CurrentIrp = nullptr;
    device->Flags = exclusive ? 0x8u : 0u; // DO_EXCLUSIVE
    device->DeviceExtension = extensionSize
        ? reinterpret_cast<UCHAR*>(device) + sizeof(GuestDeviceObject) : nullptr;
    device->DeviceType = static_cast<UCHAR>(deviceType);
    device->StartIoFlags = 0;
    device->StackSize = 1;
    device->DeletePending = FALSE;
    device->SectorSize = 512;
    device->AlignmentRequirement = 0;
    device->DeviceQueue.Type = 4;
    device->DeviceQueue.Size = static_cast<UCHAR>(sizeof(GuestDeviceQueue));
    device->DeviceQueue.Busy = 0;
    InitializeDeviceList(&device->DeviceQueue.DeviceListHead);
    device->DeviceLock.Type = 0; // NotificationEvent
    device->DeviceLock.Absolute = 0;
    device->DeviceLock.Size = static_cast<UCHAR>(sizeof(GuestEvent) / sizeof(ULONG));
    device->DeviceLock.Inserted = 0;
    device->DeviceLock.SignalState = 0;
    InitializeDeviceList(&device->DeviceLock.WaitListHead);
    device->StartIoKey = 0;
}
}

extern "C" LONG __stdcall K_IoCreateDevice(
    void* driverObject, ULONG extensionSize, void* deviceName,
    ULONG deviceType, BOOLEAN exclusive, void** deviceObject) {
    if (!deviceObject) return LONG(0xC000000Du);
    *deviceObject = nullptr;
    if (!driverObject || deviceType > 0xFFu || extensionSize > (0xFFFFu - sizeof(GuestDeviceObject)))
        return LONG(0xC000000Du);
    if (deviceName) return kStatusNotSupported; // named-device namespace is not wired yet
    if (extensionSize > SIZE_MAX - sizeof(GuestDeviceObject))
        return LONG(0xC0000017u);
    const SIZE_T totalSize = sizeof(GuestDeviceObject) + extensionSize;
    auto* device = static_cast<GuestDeviceObject*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, totalSize));
    if (!device) return LONG(0xC0000017u);
    InitializeDeviceObject(device, totalSize, driverObject, deviceType,
                           extensionSize, exclusive);
    try {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        g_deviceObjects.emplace(device, totalSize);
        g_devicePacketQueues.emplace(device, std::deque<QueuedDeviceIrp>{});
    } catch (...) {
        HeapFree(GetProcessHeap(), 0, device);
        return LONG(0xC0000017u);
    }
    *deviceObject = device;
    return 0;
}
extern "C" void __stdcall K_IoDeleteDevice(void* rawDeviceObject) {
    if (!rawDeviceObject) return;
    auto* device = static_cast<GuestDeviceObject*>(rawDeviceObject);
    SIZE_T allocationSize = 0;
    {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        auto it = g_deviceObjects.find(device);
        if (it == g_deviceObjects.end()) {
            UnsupportedIoVoidExport(68);
            return;
        }
        allocationSize = it->second;
        g_deviceObjects.erase(it);
        g_devicePacketQueues.erase(device);
    }
    device->DeletePending = TRUE;
    auto* driver = static_cast<GuestDriverObject*>(device->DriverObject);
    if (driver && driver->DriverDeleteDevice)
        driver->DriverDeleteDevice(device);
    (void)allocationSize;
    HeapFree(GetProcessHeap(), 0, device);
}
extern "C" void __stdcall K_IoFreeIrp(void* irp) {
    if (irp) UnsupportedIoVoidExport(72);
}
extern "C" void __stdcall K_IoInitializeIrp(void* irp, USHORT, CHAR) {
    if (irp) UnsupportedIoVoidExport(73);
}
extern "C" void __stdcall K_IoQueueThreadIrp(void* irp) {
    if (irp) UnsupportedIoVoidExport(77);
}
static void StartQueuedDeviceIrp(GuestDeviceObject* device,
                                 const QueuedDeviceIrp& queued) {
    auto* driver = static_cast<GuestDriverObject*>(device->DriverObject);
    if (!driver || !driver->DriverStartIo) {
        UnsupportedIoVoidExport(83);
        return;
    }
    driver->DriverStartIo(device, queued.irp);
}
extern "C" void __stdcall K_IoStartPacket(
    void* rawDeviceObject, void* irp, ULONG* key) {
    if (!rawDeviceObject || !irp) {
        UnsupportedIoVoidExport(83);
        return;
    }
    auto* device = static_cast<GuestDeviceObject*>(rawDeviceObject);
    bool startNow = false;
    QueuedDeviceIrp queued{irp, key ? *key : 0};
    {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        auto it = g_devicePacketQueues.find(device);
        if (it == g_devicePacketQueues.end()) {
            UnsupportedIoVoidExport(83);
            return;
        }
        if (!device->CurrentIrp) {
            device->CurrentIrp = irp;
            device->DeviceQueue.Busy = TRUE;
            startNow = true;
        } else {
            it->second.push_back(queued);
        }
    }
    if (startNow) StartQueuedDeviceIrp(device, queued);
}
extern "C" void __stdcall K_IoStartNextPacket(void* rawDeviceObject) {
    if (!rawDeviceObject) return;
    auto* device = static_cast<GuestDeviceObject*>(rawDeviceObject);
    QueuedDeviceIrp next{};
    bool haveNext = false;
    {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        auto it = g_devicePacketQueues.find(device);
        if (it == g_devicePacketQueues.end()) {
            UnsupportedIoVoidExport(81);
            return;
        }
        if (it->second.empty()) {
            device->CurrentIrp = nullptr;
            device->DeviceQueue.Busy = FALSE;
            return;
        }
        next = it->second.front();
        it->second.pop_front();
        device->CurrentIrp = next.irp;
        device->DeviceQueue.Busy = TRUE;
        haveNext = true;
    }
    if (haveNext) StartQueuedDeviceIrp(device, next);
}
extern "C" void __stdcall K_IoStartNextPacketByKey(
    void* rawDeviceObject, ULONG key) {
    if (!rawDeviceObject) return;
    auto* device = static_cast<GuestDeviceObject*>(rawDeviceObject);
    QueuedDeviceIrp next{};
    bool haveNext = false;
    {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        auto it = g_devicePacketQueues.find(device);
        if (it == g_devicePacketQueues.end()) {
            UnsupportedIoVoidExport(82);
            return;
        }
        auto packet = std::find_if(it->second.begin(), it->second.end(),
            [key](const QueuedDeviceIrp& item) { return item.key == key; });
        if (packet == it->second.end()) return;
        next = *packet;
        it->second.erase(packet);
        device->CurrentIrp = next.irp;
        device->DeviceQueue.Busy = TRUE;
        haveNext = true;
    }
    if (haveNext) StartQueuedDeviceIrp(device, next);
}
extern "C" LONG __stdcall K_IoSynchronousDeviceIoControlRequest(
    ULONG, void*, void*, ULONG, void*, ULONG, ULONG* returnedLength, BOOLEAN) {
    if (returnedLength) *returnedLength = 0;
    return kStatusNotSupported;
}
extern "C" LONG __stdcall K_IoSynchronousFsdRequest(
    ULONG, void*, void*, ULONG, const LARGE_INTEGER*) {
    return kStatusNotSupported;
}
extern "C" LONG __fastcall K_IofCallDriver(void*, void*) {
    return kStatusNotSupported;
}
extern "C" void __fastcall K_IofCompleteRequest(void* irp, CHAR) {
    if (irp) UnsupportedIoVoidExport(87);
}
extern "C" LONG __stdcall K_IoDismountVolume(void* rawDeviceObject) {
    if (!rawDeviceObject) return LONG(0xC000000Du);
    auto* device = static_cast<GuestDeviceObject*>(rawDeviceObject);
    {
        std::lock_guard<std::mutex> lock(g_deviceObjectMutex);
        if (g_deviceObjects.find(device) == g_deviceObjects.end())
            return kStatusNotSupported;
    }
    auto* driver = static_cast<GuestDriverObject*>(device->DriverObject);
    if (!driver || !driver->DriverDismountVolume) return kStatusNotSupported;
    return driver->DriverDismountVolume(device);
}
extern "C" LONG __stdcall K_IoDismountVolumeByName(void*) {
    return kStatusNotSupported;
}
