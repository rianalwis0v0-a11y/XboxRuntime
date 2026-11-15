#include "kernel_io_exports.h"

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
extern "C" LONG __stdcall K_IoCheckShareAccess(
    ACCESS_MASK, ULONG, void*, void*, BOOLEAN) { return kStatusNotSupported; }
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
