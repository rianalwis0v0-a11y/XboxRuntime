#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "kernel_memory.h"
#include "../guest32/kernel_object_manager.h"

static int Fail(const char* what, LONG status) {
    std::fprintf(stderr, "kernel_handle_guest_smoke: %s status=0x%08lX\n",
                 what, static_cast<unsigned long>(status));
    return 1;
}
namespace {
struct TestPayload { int destroyed = 0; int state = 0; };
void DestroyTestPayload(void* raw) { static_cast<TestPayload*>(raw)->destroyed++; }
}
int main() {
    static_assert(sizeof(void*) == 4, "Xbox guest ABI test must be x86");
    HANDLE source = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!source) return Fail("create source event", static_cast<LONG>(GetLastError()));

    HANDLE duplicate = nullptr;
    LONG status = K_NtDuplicateObject(source, &duplicate, 1u); // NXDK DUPLICATE_CLOSE_SOURCE
    if (status != 0 || !duplicate) return Fail("duplicate/close-source", status);

    SetLastError(ERROR_SUCCESS);
    if (WaitForSingleObject(source, 0) != WAIT_FAILED ||
        GetLastError() != ERROR_INVALID_HANDLE)
        return Fail("source handle must be closed", static_cast<LONG>(GetLastError()));

    if (!SetEvent(duplicate) || WaitForSingleObject(duplicate, 0) != WAIT_OBJECT_0)
        return Fail("duplicate must retain source access", static_cast<LONG>(GetLastError()));

    if (!CloseHandle(duplicate))
        return Fail("close duplicate", static_cast<LONG>(GetLastError()));

    HANDLE optionSource = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!optionSource) return Fail("create option-validation source", static_cast<LONG>(GetLastError()));
    HANDLE invalidTarget = nullptr;
    status = K_NtDuplicateObject(optionSource, &invalidTarget, 2u);
    CloseHandle(optionSource);
    if (status != static_cast<LONG>(0xC000000Du))
        return Fail("reject unsupported duplicate options", status);

    TestPayload payload{};
    HANDLE objectHandle = XboxKernelCreateObjectHandle(&payload, XboxKernelObjectType::IoCompletion, DestroyTestPayload);
    if (!objectHandle) return Fail("register shared object", LONG(0xC0000017u));
    void* acquired = reinterpret_cast<void*>(1);
    if (XboxKernelAcquireObjectReference(objectHandle, XboxKernelObjectType::Event, &acquired) || acquired != nullptr || XboxKernelGetLastObjectLookupStatus() != XboxKernelObjectLookupStatus::TypeMismatch)
        return Fail("type mismatch must clear output and report mismatch", LONG(0xC0000024u));
    if (!XboxKernelAcquireObjectReference(objectHandle, XboxKernelObjectType::IoCompletion, &acquired) || acquired != &payload)
        return Fail("acquire shared object", LONG(0xC0000008u));
    HANDLE objectDuplicate = nullptr;
    status = XboxKernelDuplicateObjectHandle(objectHandle, &objectDuplicate, 0x2u);
    if (status != 0 || !objectDuplicate) return Fail("duplicate shared object", status);
    payload.state = 42;
    if (!XboxKernelCloseObjectHandle(objectHandle) || payload.destroyed != 0)
        return Fail("closing first handle must preserve object", LONG(0xC0000001u));
    void* duplicatePayload = nullptr;
    if (!XboxKernelAcquireObjectReference(objectDuplicate, XboxKernelObjectType::IoCompletion, &duplicatePayload) || duplicatePayload != &payload || static_cast<TestPayload*>(duplicatePayload)->state != 42)
        return Fail("duplicate must share object identity and state", LONG(0xC0000001u));
    XboxKernelReleaseObjectReference(duplicatePayload);
    if (!XboxKernelCloseObjectHandle(objectDuplicate) || payload.destroyed != 0)
        return Fail("acquired reference must keep object alive", LONG(0xC0000001u));
    XboxKernelReleaseObjectReference(acquired);
    if (payload.destroyed != 1) return Fail("final release must destroy exactly once", LONG(0xC0000001u));

    TestPayload shutdownPayload{};
    HANDLE shutdownHandle = XboxKernelCreateObjectHandle(&shutdownPayload, XboxKernelObjectType::Event, DestroyTestPayload);
    void* shutdownReference = nullptr;
    if (!shutdownHandle || !XboxKernelAcquireObjectReference(shutdownHandle, XboxKernelObjectType::Event, &shutdownReference))
        return Fail("prepare shutdown reference", LONG(0xC0000001u));
    XboxKernelCloseAllObjectHandles();
    if (shutdownPayload.destroyed != 0) return Fail("shutdown must wait for outstanding references", LONG(0xC0000001u));
    XboxKernelReleaseObjectReference(shutdownReference);
    if (shutdownPayload.destroyed != 1) return Fail("shutdown reference drain", LONG(0xC0000001u));
    XboxKernelBeginObjectHandleSession();

    std::puts("kernel_handle_guest_smoke: PASS (native duplicate + shared guest object lifecycle)");
    return 0;
}
