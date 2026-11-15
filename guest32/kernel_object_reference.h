#pragma once

extern "C" {
LONG __stdcall K_ObReferenceObjectByHandle(HANDLE handle, void* objectType, void** returnedObject);
void __fastcall K_ObfReferenceObject(void* object);
void __fastcall K_ObfDereferenceObject(void* object);
}
