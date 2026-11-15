#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "kernel_io_completion.h"
#include "kernel_object_manager.h"

struct CompletionBasicInformation { LONG Depth; };
struct GuestIoStatusBlock { union { LONG Status; void* Pointer; }; ULONG Information; };

int main() {
    static_assert(sizeof(void*) == 4, "Xbox completion smoke test requires x86");
    HANDLE completion = nullptr;
    LONG status = K_NtCreateIoCompletion(&completion, 0, nullptr, 1);
    if (status != 0 || !completion) {
        std::fprintf(stderr, "NtCreateIoCompletion failed: 0x%08lX\n",
                     static_cast<unsigned long>(status));
        return 1;
    }

    void* object = nullptr;
    if (!XboxKernelAcquireObjectReference(completion,
            XboxKernelObjectType::IoCompletion, &object) || !object)
        return 2;
    status = K_IoSetIoCompletion(object, reinterpret_cast<void*>(0x1111),
        reinterpret_cast<void*>(0x2222), static_cast<LONG>(0xC0000001u), 0x3344);
    XboxKernelReleaseObjectReference(object);
    if (status != 0) return 3;
    status = K_IoSetIoCompletion(completion, reinterpret_cast<void*>(0x3333),
        reinterpret_cast<void*>(0x4444), static_cast<LONG>(0xC0000002u), 0x5566);
    if (status != 0) return 13;

    HANDLE duplicate = nullptr;
    status = XboxKernelDuplicateObjectHandle(completion, &duplicate, 0x2);
    if (status != 0 || !duplicate) return 4;
    status = K_NtSetIoCompletion(duplicate, reinterpret_cast<void*>(0xAAAA),
        reinterpret_cast<void*>(0xBBBB), 0, 0xCCCC);
    if (status != 0) return 5;

    CompletionBasicInformation depth{};
    if (K_NtQueryIoCompletion(completion, &depth) != 0 || depth.Depth != 3)
        return 6;

    void* key = nullptr;
    void* apc = nullptr;
    GuestIoStatusBlock io{};
    LARGE_INTEGER noWait{};
    status = K_NtRemoveIoCompletion(duplicate, &key, &apc, &io, &noWait);
    if (status != 0 || key != reinterpret_cast<void*>(0x1111) ||
        apc != reinterpret_cast<void*>(0x2222) ||
        io.Status != static_cast<LONG>(0xC0000001u) || io.Information != 0x3344)
        return 7;

    status = K_NtRemoveIoCompletion(completion, &key, &apc, &io, &noWait);
    if (status != 0 || key != reinterpret_cast<void*>(0x3333) ||
        apc != reinterpret_cast<void*>(0x4444) ||
        io.Status != static_cast<LONG>(0xC0000002u) || io.Information != 0x5566)
        return 8;

    status = K_NtRemoveIoCompletion(completion, &key, &apc, &io, &noWait);
    if (status != 0 || key != reinterpret_cast<void*>(0xAAAA) ||
        apc != reinterpret_cast<void*>(0xBBBB) || io.Status != 0 ||
        io.Information != 0xCCCC)
        return 14;

    if (K_NtQueryIoCompletion(completion, &depth) != 0 || depth.Depth != 0)
        return 9;
    if (K_NtRemoveIoCompletion(completion, &key, &apc, &io, &noWait) != 0x102)
        return 10;

    if (!XboxKernelCloseObjectHandle(completion) ||
        !XboxKernelCloseObjectHandle(duplicate))
        return 11;
    if (K_NtQueryIoCompletion(completion, &depth) != static_cast<LONG>(0xC0000008u))
        return 12;

    std::puts("kernel_io_completion_guest_smoke: PASS (Nt/Io set, query, remove, duplicate, close)");
    return 0;
}
