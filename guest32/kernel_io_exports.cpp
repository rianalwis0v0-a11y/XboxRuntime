#include "kernel_io_exports.h"

#include <mutex>

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
        (access & (0x00000001u | 0x00000020u | kGenericRead | kGenericAll)) != 0,
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

extern "C" LONG __stdcall K_IoCreateDevice(
    void*, ULONG, void*, ULONG, BOOLEAN, void** deviceObject) {
    if (deviceObject) *deviceObject = nullptr;
    return kStatusNotSupported;
}
extern "C" void __stdcall K_IoDeleteDevice(void* deviceObject) {
    if (deviceObject) UnsupportedIoVoidExport(68);
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
extern "C" void __stdcall K_IoRemoveShareAccess(void* fileObject, void*) {
    if (fileObject) UnsupportedIoVoidExport(78);
}
extern "C" void __stdcall K_IoSetShareAccess(ACCESS_MASK, ULONG,
                                               void* fileObject, void*) {
    if (fileObject) UnsupportedIoVoidExport(80);
}
extern "C" void __stdcall K_IoStartNextPacket(void* deviceObject) {
    if (deviceObject) UnsupportedIoVoidExport(81);
}
extern "C" void __stdcall K_IoStartNextPacketByKey(void* deviceObject, ULONG) {
    if (deviceObject) UnsupportedIoVoidExport(82);
}
extern "C" void __stdcall K_IoStartPacket(void* deviceObject, void*, ULONG*) {
    if (deviceObject) UnsupportedIoVoidExport(83);
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
extern "C" LONG __stdcall K_IoDismountVolume(void*) {
    return kStatusNotSupported;
}
extern "C" LONG __stdcall K_IoDismountVolumeByName(void*) {
    return kStatusNotSupported;
}
