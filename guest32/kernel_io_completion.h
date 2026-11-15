#pragma once
#include <windows.h>
#include <cstdint>

extern "C" {
LONG __stdcall K_NtCreateIoCompletion(HANDLE* out, ACCESS_MASK access,
                                      void* objectAttributes, ULONG count);
LONG __stdcall K_NtSetIoCompletion(HANDLE handle, void* keyContext,
    void* apcContext, LONG ioStatus, ULONG_PTR information);
LONG __stdcall K_NtRemoveIoCompletion(HANDLE handle, void** keyContext,
    void** apcContext, void* ioStatusBlock, const LARGE_INTEGER* timeout);
LONG __stdcall K_NtQueryIoCompletion(HANDLE handle, void* information);
LONG __stdcall K_IoSetIoCompletion(void* ioCompletion, void* keyContext,
    void* apcContext, LONG ioStatus, ULONG_PTR information);
}
