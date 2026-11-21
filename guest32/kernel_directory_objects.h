#pragma once
#include <windows.h>

extern "C" LONG __stdcall K_NtCreateDirectoryObject(HANDLE* directoryHandle,
                                                     void* objectAttributes);

extern "C" LONG __stdcall K_NtOpenDirectoryObject(HANDLE* directoryHandle,
                                                     ULONG desiredAccess,
                                                     void* objectAttributes);

extern "C" LONG __stdcall K_NtQueryDirectoryObject(HANDLE directoryHandle,
    void* buffer, ULONG length, BOOLEAN restartScan, ULONG* context,
    ULONG* returnLength);
