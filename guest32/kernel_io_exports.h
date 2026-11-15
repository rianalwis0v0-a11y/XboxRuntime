#pragma once
#include <windows.h>
#include <cstdint>

extern "C" {
void* __stdcall K_IoAllocateIrp(CHAR stackSize);
void* __stdcall K_IoBuildAsynchronousFsdRequest(ULONG majorFunction,
    void* deviceObject, void* buffer, ULONG length,
    const LARGE_INTEGER* startingOffset, void* ioStatusBlock);
void* __stdcall K_IoBuildDeviceIoControlRequest(ULONG controlCode,
    void* deviceObject, void* inputBuffer, ULONG inputLength,
    void* outputBuffer, ULONG outputLength, BOOLEAN internalControl,
    void* event, void* ioStatusBlock);
void* __stdcall K_IoBuildSynchronousFsdRequest(ULONG majorFunction,
    void* deviceObject, void* buffer, ULONG length,
    const LARGE_INTEGER* startingOffset, void* event, void* ioStatusBlock);
LONG __stdcall K_IoCheckShareAccess(ACCESS_MASK desiredAccess,
    ULONG desiredShareAccess, void* fileObject, void* shareAccess,
    BOOLEAN update);
LONG __stdcall K_IoCreateDevice(void* driverObject, ULONG extensionSize,
    void* deviceName, ULONG deviceType, BOOLEAN exclusive, void** deviceObject);
void __stdcall K_IoDeleteDevice(void* deviceObject);
void __stdcall K_IoFreeIrp(void* irp);
void __stdcall K_IoInitializeIrp(void* irp, USHORT packetSize, CHAR stackSize);
void __stdcall K_IoQueueThreadIrp(void* irp);
void __stdcall K_IoRemoveShareAccess(void* fileObject, void* shareAccess);
void __stdcall K_IoSetShareAccess(ACCESS_MASK desiredAccess,
    ULONG desiredShareAccess, void* fileObject, void* shareAccess);
void __stdcall K_IoStartNextPacket(void* deviceObject);
void __stdcall K_IoStartNextPacketByKey(void* deviceObject, ULONG key);
void __stdcall K_IoStartPacket(void* deviceObject, void* irp, ULONG* key);
LONG __stdcall K_IoSynchronousDeviceIoControlRequest(ULONG controlCode,
    void* deviceObject, void* inputBuffer, ULONG inputLength,
    void* outputBuffer, ULONG outputLength, ULONG* returnedLength,
    BOOLEAN internalControl);
LONG __stdcall K_IoSynchronousFsdRequest(ULONG majorFunction,
    void* deviceObject, void* buffer, ULONG length,
    const LARGE_INTEGER* startingOffset);
LONG __fastcall K_IofCallDriver(void* deviceObject, void* irp);
void __fastcall K_IofCompleteRequest(void* irp, CHAR priorityBoost);
LONG __stdcall K_IoDismountVolume(void* deviceObject);
LONG __stdcall K_IoDismountVolumeByName(void* deviceName);
}
