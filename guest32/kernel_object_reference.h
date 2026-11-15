#pragma once
#include <windows.h>

extern "C" {
LONG __stdcall K_ObReferenceObjectByHandle(HANDLE handle, void* objectType, void** returnedObject);
LONG __stdcall K_ObReferenceObjectByPointer(void* object, ULONG desiredAccess, void* objectType, ULONG accessMode);
void __fastcall K_ObfReferenceObject(void* object);
void __fastcall K_ObfDereferenceObject(void* object);
}
