#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "kernel_object_manager.h"
#include "kernel_object_reference.h"

namespace {
struct TestObject { int* destroyed; };
void DestroyTestObject(void* raw) {
    auto* object=static_cast<TestObject*>(raw);
    ++*object->destroyed;
    delete object;
}
int Fail(const char* label) {
    std::fprintf(stderr,"kernel_object_reference_guest_smoke: FAIL: %s\n",label);
    return 1;
}
}
int main() {
    static_assert(sizeof(void*)==4,"object reference ABI test must be x86");
    XboxKernelBeginObjectHandleSession();
    int destroyed=0;
    auto* object=new TestObject{&destroyed};
    HANDLE handle=XboxKernelCreateObjectHandle(object,XboxKernelObjectType::Directory,DestroyTestObject);
    if (!handle) return Fail("create manager-owned object");
    K_ObfReferenceObject(object);
    if (!XboxKernelCloseObjectHandle(handle) || destroyed!=0)
        return Fail("handle close preserves outstanding body reference");
    K_ObfDereferenceObject(object);
    if (destroyed!=1) return Fail("final dereference destroys exactly once");
    if (XboxKernelReferenceObject(nullptr) || XboxKernelDereferenceObject(nullptr))
        return Fail("null object references rejected");

    HANDLE native=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!native) return Fail("create native event handle");
    K_ObfDereferenceObject(reinterpret_cast<void*>(native));
    DWORD flags=0;
    if (!GetHandleInformation(native,&flags)) return Fail("native handle dereference remains a no-op");
    CloseHandle(native);
    std::puts("kernel_object_reference_guest_smoke: PASS (x86 fastcall, refcount lifetime, native HANDLE compatibility)");
    return 0;
}
