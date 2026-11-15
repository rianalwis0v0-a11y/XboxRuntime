#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "kernel_object_manager.h"
#include "kernel_object_reference.h"

namespace {
void RaiseUnsupportedObjectReference(std::uint32_t ordinal) {
    ULONG_PTR parameter=ordinal;
    RaiseException(0xC000001Du, EXCEPTION_NONCONTINUABLE, 1, &parameter);
}
bool WritableGuestSpan(const void* pointer, std::size_t length) {
    if (!pointer || !length) return false;
    const std::uintptr_t start=reinterpret_cast<std::uintptr_t>(pointer);
    if (length > UINTPTR_MAX-start) return false;
    const std::uintptr_t end=start+length;
    std::uintptr_t cursor=start;
    while (cursor<end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD|PAGE_NOACCESS)))
            return false;
        const DWORD protection=memory.Protect & 0xFFu;
        if (protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
            protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY)
            return false;
        const std::uintptr_t region=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > UINTPTR_MAX-region) return false;
        const std::uintptr_t next=region+memory.RegionSize;
        if (next<=cursor) return false;
        cursor=(std::min)(next,end);
    }
    return true;
}
}
extern "C" LONG __stdcall K_ObReferenceObjectByHandle(HANDLE handle, void* objectType, void** returnedObject) {
    constexpr LONG kStatusAccessViolation=static_cast<LONG>(0xC0000005u);
    constexpr LONG kStatusObjectTypeMismatch=static_cast<LONG>(0xC0000024u);
    if (!WritableGuestSpan(returnedObject, sizeof(*returnedObject)))
        return kStatusAccessViolation;
    *returnedObject=nullptr;
    void* object=nullptr;
    if (!XboxKernelAcquireAnyObjectReference(handle, &object))
        return static_cast<LONG>(XboxKernelGetLastObjectLookupStatus());
    if (objectType) {
        XboxKernelReleaseObjectReference(object);
        return kStatusObjectTypeMismatch;
    }
    *returnedObject=object;
    return 0;
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
