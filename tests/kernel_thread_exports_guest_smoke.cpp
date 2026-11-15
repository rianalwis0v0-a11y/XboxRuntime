#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "kernel_thread_exports.h"

namespace {
volatile LONG g_normalCalled = 0;
volatile LONG g_syncCalled = 0;
void __stdcall NormalRoutine(void* context, void* arg1, void* arg2) {
    if (context == reinterpret_cast<void*>(0x1234) &&
        arg1 == reinterpret_cast<void*>(0x5678) &&
        arg2 == reinterpret_cast<void*>(0x9ABC))
        InterlockedIncrement(&g_normalCalled);
}
BOOLEAN __stdcall SyncRoutine(void* context) {
    if (context == reinterpret_cast<void*>(0xCAFE))
        InterlockedIncrement(&g_syncCalled);
    return TRUE;
}
BOOLEAN __stdcall ServiceRoutine(void*, void*) { return TRUE; }
}
int main() {
    static_assert(sizeof(void*) == 4, "guest thread exports require x86 ABI");
    int apcStorage = 0;
    K_KeInitializeApc(&apcStorage, GetCurrentThread(), nullptr, nullptr,
                      NormalRoutine, 1, reinterpret_cast<void*>(0x1234));
    if (!K_KeInsertQueueApc(&apcStorage, reinterpret_cast<void*>(0x5678),
                            reinterpret_cast<void*>(0x9ABC), 0))
        return 1;
    if (SleepEx(2000, TRUE) != WAIT_IO_COMPLETION ||
        g_normalCalled != 1) {
        std::fprintf(stderr, "APC did not run through an alertable wait\n");
        return 2;
    }

    if (!K_KeAlertThread(GetCurrentThread(), 1) ||
        SleepEx(2000, TRUE) != WAIT_IO_COMPLETION) {
        std::fprintf(stderr, "KeAlertThread did not alert the target thread\n");
        return 3;
    }

    int interruptStorage = 0;
    K_KeInitializeInterrupt(&interruptStorage, ServiceRoutine, nullptr,
                            0x31, 5, 0, FALSE);
    if (!K_KeSynchronizeExecution(&interruptStorage, SyncRoutine,
                                  reinterpret_cast<void*>(0xCAFE)) ||
        g_syncCalled != 1) {
        std::fprintf(stderr, "KeSynchronizeExecution callback failed\n");
        return 4;
    }
    if (K_KeConnectInterrupt(&interruptStorage) ||
        K_KeDisconnectInterrupt(&interruptStorage)) {
        std::fprintf(stderr, "hardware interrupt controller must not report connected\n");
        return 5;
    }

    K_HalRequestSoftwareInterrupt(2);
    K_HalClearSoftwareInterrupt(2);
    std::puts("kernel_thread_exports_guest_smoke: PASS (APC, alert, interrupt synchronization, software IRQ)");
    return 0;
}
