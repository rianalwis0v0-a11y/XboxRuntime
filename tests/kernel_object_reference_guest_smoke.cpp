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

    // Pointer reference increments the shared object-body refcount.
    int pointerDestroyed=0;
    auto* pointerObject=new TestObject{&pointerDestroyed};
    HANDLE pointerHandle=XboxKernelCreateObjectHandle(
        pointerObject,XboxKernelObjectType::Directory,DestroyTestObject);
    if (!pointerHandle) return Fail("create object for ObReferenceObjectByPointer");
    if (K_ObReferenceObjectByPointer(pointerObject,0,nullptr,0)!=0)
        return Fail("generic ObReferenceObjectByPointer");
    if (K_ObReferenceObjectByPointer(pointerObject,0,reinterpret_cast<void*>(1),0)!=
        static_cast<LONG>(0xC0000024u))
        return Fail("typed pointer reference rejects unknown OBJECT_TYPE");
    if (!XboxKernelCloseObjectHandle(pointerHandle) || pointerDestroyed!=0)
        return Fail("pointer reference survives handle close");
    K_ObfDereferenceObject(pointerObject);
    if (pointerDestroyed!=1) return Fail("pointer reference final dereference");
    if (K_ObReferenceObjectByPointer(nullptr,0,nullptr,0)!=
        static_cast<LONG>(0xC000000Du))
        return Fail("null pointer reference rejected");

    // Generic (ObjectType == NULL) handle reference increments the shared object refcount.
    int referencedDestroyed=0;
    auto* referenced=new TestObject{&referencedDestroyed};
    HANDLE referencedHandle=XboxKernelCreateObjectHandle(
        referenced,XboxKernelObjectType::Directory,DestroyTestObject);
    if (!referencedHandle) return Fail("create object for ObReferenceObjectByHandle");
    void* returned=nullptr;
    if (K_ObReferenceObjectByHandle(referencedHandle,nullptr,&returned)!=0 || returned!=referenced)
        return Fail("generic ObReferenceObjectByHandle");
    if (!XboxKernelCloseObjectHandle(referencedHandle) || referencedDestroyed!=0)
        return Fail("reference-by-handle survives handle close");
    K_ObfDereferenceObject(referenced);
    if (referencedDestroyed!=1) return Fail("reference-by-handle final dereference");

    void* invalidOut=reinterpret_cast<void*>(0x1234);
    if (K_ObReferenceObjectByHandle(reinterpret_cast<HANDLE>(0xE0000000u),nullptr,&invalidOut)!=
        static_cast<LONG>(0xC0000008u) || invalidOut!=nullptr)
        return Fail("invalid object handle status and null output");

    int badOutputDestroyed=0;
    auto* badOutput=new TestObject{&badOutputDestroyed};
    HANDLE badOutputHandle=XboxKernelCreateObjectHandle(
        badOutput,XboxKernelObjectType::Directory,DestroyTestObject);
    if (!badOutputHandle) return Fail("create object for invalid output test");
    if (K_ObReferenceObjectByHandle(badOutputHandle,nullptr,reinterpret_cast<void**>(1))!=
        static_cast<LONG>(0xC0000005u))
        return Fail("invalid returned-object pointer status");
    if (!XboxKernelCloseObjectHandle(badOutputHandle) || badOutputDestroyed!=1)
        return Fail("invalid output pointer does not leak a reference");

    int mismatchDestroyed=0;
    auto* mismatch=new TestObject{&mismatchDestroyed};
    HANDLE mismatchHandle=XboxKernelCreateObjectHandle(
        mismatch,XboxKernelObjectType::Directory,DestroyTestObject);
    void* mismatchOut=reinterpret_cast<void*>(0x1234);
    if (K_ObReferenceObjectByHandle(mismatchHandle,reinterpret_cast<void*>(1),&mismatchOut)!=
        static_cast<LONG>(0xC0000024u) || mismatchOut!=nullptr)
        return Fail("typed reference rejects unknown OBJECT_TYPE");
    if (mismatchDestroyed!=0 || !XboxKernelCloseObjectHandle(mismatchHandle) || mismatchDestroyed!=1)
        return Fail("type mismatch releases temporary reference");

    HANDLE native=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!native) return Fail("create native event handle");
    K_ObfDereferenceObject(reinterpret_cast<void*>(native));
    DWORD flags=0;
    if (!GetHandleInformation(native,&flags)) return Fail("native handle dereference remains a no-op");
    CloseHandle(native);
    std::puts("kernel_object_reference_guest_smoke: PASS (x86 fastcall, refcount lifetime, native HANDLE compatibility)");
    return 0;
}
