#pragma once
#include <windows.h>

extern "C" LONG __stdcall K_NtCreateDirectoryObject(HANDLE* directoryHandle,
                                                     void* objectAttributes);
