#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
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
    if (!K_KeConnectInterrupt(&interruptStorage) ||
        K_KeConnectInterrupt(&interruptStorage)) {
        std::fprintf(stderr, "virtual interrupt connect lifecycle failed\n");
        return 4;
    }
    if (!K_KeSynchronizeExecution(&interruptStorage, SyncRoutine,
                                  reinterpret_cast<void*>(0xCAFE)) ||
        g_syncCalled != 1) {
        std::fprintf(stderr, "KeSynchronizeExecution callback failed\n");
        return 5;
    }
    if (!K_KeDisconnectInterrupt(&interruptStorage) ||
        K_KeDisconnectInterrupt(&interruptStorage) ||
        K_KeSynchronizeExecution(&interruptStorage, SyncRoutine,
                                 reinterpret_cast<void*>(0xCAFE))) {
        std::fprintf(stderr, "virtual interrupt disconnect lifecycle failed\n");
        return 6;
    }

    std::uint32_t nv2aId = 0;
    K_HalReadWritePCISpace(1, 0, 0x00, &nv2aId, sizeof(nv2aId), FALSE);
    if (nv2aId != 0x02A010DEu) {
        std::fprintf(stderr, "NV2A PCI vendor/device ID mismatch: 0x%08X\n", nv2aId);
        return 7;
    }
    std::uint32_t hostBridgeId = 0;
    K_HalReadWritePCISpace(0, 0, 0x00, &hostBridgeId, sizeof(hostBridgeId), FALSE);
    if (hostBridgeId != 0x02A510DEu) {
        std::fprintf(stderr, "Xbox host bridge PCI ID mismatch: 0x%08X\n", hostBridgeId);
        return 8;
    }
    std::uint32_t fsbRatio = 0;
    K_HalReadWritePCISpace(0, 0x60u, 0x6Cu, &fsbRatio, sizeof(fsbRatio), FALSE);
    if (fsbRatio != 0x00000801u) {
        std::fprintf(stderr, "NXDK host-bridge FSB ratio mismatch: 0x%08X\\n", fsbRatio);
        return 15;
    }
    std::uint8_t fsbDivider = 0, fsbMultiplier = 0;
    K_HalReadWritePCISpace(0, 0x60u, 0x6Cu, &fsbDivider, 1, FALSE);
    K_HalReadWritePCISpace(0, 0x60u, 0x6Du, &fsbMultiplier, 1, FALSE);
    if (fsbDivider != 1u || fsbMultiplier != 8u) {
        std::fprintf(stderr, "NXDK PCI clock byte lanes mismatch: %u/%u\\n",
                     fsbDivider, fsbMultiplier);
        return 16;
    }
    std::uint32_t agpBridgeId = 0;
    K_HalReadWritePCISpace(0, 0x1Eu, 0x00, &agpBridgeId, sizeof(agpBridgeId), FALSE);
    if (agpBridgeId != 0x01B710DEu) {
        std::fprintf(stderr, "Xbox AGP bridge PCI ID mismatch: 0x%08X\n", agpBridgeId);
        return 14;
    }
    std::uint32_t absentId = 0;
    K_HalReadWritePCISpace(1, 1, 0x00, &absentId, sizeof(absentId), FALSE);
    if (absentId != 0xFFFFFFFFu) {
        std::fprintf(stderr, "absent PCI function did not read as all ones\n");
        return 9;
    }
    std::uint32_t bar0 = 0;
    K_HalReadWritePCISpace(1, 0, 0x10, &bar0, sizeof(bar0), FALSE);
    if (bar0 != 0xFD000000u) {
        std::fprintf(stderr, "NV2A BAR0 mismatch: 0x%08X\n", bar0);
        return 10;
    }
    std::uint32_t barProbe = 0xFFFFFFFFu;
    K_HalReadWritePCISpace(1, 0, 0x10, &barProbe, sizeof(barProbe), TRUE);
    barProbe = 0;
    K_HalReadWritePCISpace(1, 0, 0x10, &barProbe, sizeof(barProbe), FALSE);
    if (barProbe != 0xFF000000u) {
        std::fprintf(stderr, "NV2A BAR0 size probe mismatch: 0x%08X\n", barProbe);
        return 11;
    }
    bar0 = 0xFD000000u;
    K_HalReadWritePCISpace(1, 0, 0x10, &bar0, sizeof(bar0), TRUE);
    std::uint16_t command = 0;
    K_HalReadWritePCISpace(1, 0, 0x04, &command, sizeof(command), FALSE);
    if (command != 0x0007u) {
        std::fprintf(stderr, "NV2A PCI command register initial value mismatch\n");
        return 12;
    }
    command = 0x0003u;
    K_HalReadWritePCISpace(1, 0, 0x04, &command, sizeof(command), TRUE);
    command = 0;
    K_HalReadWritePCISpace(1, 0, 0x04, &command, sizeof(command), FALSE);
    if (command != 0x0003u) {
        std::fprintf(stderr, "NV2A PCI command register write failed\n");
        return 13;
    }
    command = 0x0007u;
    K_HalReadWritePCISpace(1, 0, 0x04, &command, sizeof(command), TRUE);

    K_HalRequestSoftwareInterrupt(2);
    K_HalClearSoftwareInterrupt(2);
    std::puts("kernel_thread_exports_guest_smoke: PASS (APC, interrupt, NV2A PCI config, software IRQ)");
    return 0;
}
