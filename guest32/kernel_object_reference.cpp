#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "kernel_object_manager.h"
#include "kernel_object_reference.h"

namespace {
void RaiseUnsupportedObjectReference(std::uint32_t ordinal) {
    ULONG_PTR parameter=ordinal;
    RaiseException(0xC000001Du, EXCEPTION_NONCONTINUABLE, 1, &parameter);
}
}
extern "C" void __fastcall K_ObfReferenceObject(void* object) {
    if (!XboxKernelReferenceObject(object))
        RaiseUnsupportedObjectReference(251);
}
extern "C" void __fastcall K_ObfDereferenceObject(void* object) {
    // Cxbx compatibility note: dereferencing a native Win32 HANDLE is a no-op.
    // The guest object manager's object-body pointers are separately tracked.
    DWORD flags=0;
    if (object && GetHandleInformation(reinterpret_cast<HANDLE>(object), &flags))
        return;
    if (!XboxKernelDereferenceObject(object))
        RaiseUnsupportedObjectReference(250);
}
