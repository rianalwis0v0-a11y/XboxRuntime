#include "kernel_thread_exports.h"

#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <mutex>
#include <unordered_map>

namespace {
using KernelApcRoutine = void (__stdcall*)(void*, XboxKernelNormalRoutine*, void**, void**, void**);
using RundownRoutine = void (__stdcall*)(void*);
using NormalRoutine = void (__stdcall*)(void*, void*, void*);
using ServiceRoutine = BOOLEAN (__stdcall*)(void*, void*);
using SynchronizeRoutine = BOOLEAN (__stdcall*)(void*);

struct ApcRecord {
    HANDLE thread = nullptr;
    KernelApcRoutine kernel = nullptr;
    RundownRoutine rundown = nullptr;
    NormalRoutine normal = nullptr;
    UCHAR mode = 0;
    void* normalContext = nullptr;
};
struct ApcWork {
    void* apc;
    void* arg1;
    void* arg2;
};
struct InterruptRecord {
    ServiceRoutine service = nullptr;
    void* context = nullptr;
    ULONG vector = 0;
    UCHAR irql = 0;
    ULONG mode = 0;
    BOOLEAN shared = FALSE;
    bool connected = false;
    std::mutex synchronization;
};
std::mutex g_apcMutex;
std::unordered_map<void*, ApcRecord> g_apcs;
std::mutex g_interruptMutex;
std::unordered_map<void*, std::shared_ptr<InterruptRecord>> g_interrupts;
struct SystemInterruptRecord { bool enabled = false; ULONG mode = 0; };
std::mutex g_systemInterruptMutex;
std::array<SystemInterruptRecord, 256> g_systemInterrupts{};
thread_local UCHAR g_pendingSoftwareIrql = 0;

void CALLBACK DispatchApc(ULONG_PTR parameter) {
    std::unique_ptr<ApcWork> work(reinterpret_cast<ApcWork*>(parameter));
    if (!work) return;
    ApcRecord record{};
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        auto it = g_apcs.find(work->apc);
        if (it == g_apcs.end()) return;
        record = it->second;
    }
    auto normal = record.normal;
    void* normalContext = record.normalContext;
    void* arg1 = work->arg1;
    void* arg2 = work->arg2;
    if (record.kernel)
        record.kernel(work->apc, &normal,
                      &normalContext, &arg1, &arg2);
    if (normal) normal(normalContext, arg1, arg2);
}
void CALLBACK AlertApc(ULONG_PTR) {}
}

extern "C" void __fastcall K_HalRequestSoftwareInterrupt(UCHAR requestIrql) {
    g_pendingSoftwareIrql = (std::max)(g_pendingSoftwareIrql, requestIrql);
}
extern "C" void __fastcall K_HalClearSoftwareInterrupt(UCHAR requestIrql) {
    if (g_pendingSoftwareIrql <= requestIrql) g_pendingSoftwareIrql = 0;
}
extern "C" BOOLEAN __stdcall K_KeAlertThread(HANDLE thread, UCHAR) {
    if (!thread || thread == INVALID_HANDLE_VALUE) return FALSE;
    return QueueUserAPC(AlertApc, thread, 0) != 0 ? TRUE : FALSE;
}
extern "C" void __stdcall K_KeInitializeApc(
    void* apc, void* thread, KernelApcRoutine kernelRoutine,
    RundownRoutine rundownRoutine, NormalRoutine normalRoutine,
    UCHAR processorMode, void* normalContext) {
    if (!apc || !thread) return;
    std::lock_guard<std::mutex> lock(g_apcMutex);
    g_apcs[apc] = ApcRecord{static_cast<HANDLE>(thread), kernelRoutine,
        rundownRoutine, normalRoutine, processorMode, normalContext};
}
extern "C" BOOLEAN __stdcall K_KeInsertQueueApc(
    void* apc, void* systemArgument1, void* systemArgument2, LONG) {
    if (!apc) return FALSE;
    ApcRecord record{};
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        auto it = g_apcs.find(apc);
        if (it == g_apcs.end()) return FALSE;
        record = it->second;
    }
    auto* work = new (std::nothrow) ApcWork{apc, systemArgument1, systemArgument2};
    if (!work) return FALSE;
    if (!QueueUserAPC(DispatchApc, record.thread,
                      reinterpret_cast<ULONG_PTR>(work))) {
        delete work;
        return FALSE;
    }
    return TRUE;
}
extern "C" void __stdcall K_KeInitializeInterrupt(
    void* interrupt, ServiceRoutine serviceRoutine, void* serviceContext,
    ULONG vector, UCHAR irql, ULONG interruptMode, BOOLEAN shareVector) {
    if (!interrupt || !serviceRoutine) return;
    auto record = std::make_shared<InterruptRecord>();
    record->service = serviceRoutine;
    record->context = serviceContext;
    record->vector = vector;
    record->irql = irql;
    record->mode = interruptMode;
    record->shared = shareVector;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    g_interrupts[interrupt] = std::move(record);
}
extern "C" BOOLEAN __stdcall K_KeConnectInterrupt(void* interrupt) {
    if (!interrupt) return FALSE;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    auto it = g_interrupts.find(interrupt);
    if (it == g_interrupts.end() || it->second->connected) return FALSE;
    // This registers the virtual interrupt object. Physical device interrupt
    // delivery remains unavailable until a bus/interrupt-controller backend exists.
    it->second->connected = true;
    return TRUE;
}
extern "C" BOOLEAN __stdcall K_KeDisconnectInterrupt(void* interrupt) {
    if (!interrupt) return FALSE;
    std::lock_guard<std::mutex> lock(g_interruptMutex);
    auto it = g_interrupts.find(interrupt);
    if (it == g_interrupts.end() || !it->second->connected) return FALSE;
    it->second->connected = false;
    return TRUE;
}
extern "C" BOOLEAN __stdcall K_KeSynchronizeExecution(
    void* interrupt, SynchronizeRoutine synchronizeRoutine,
    void* synchronizeContext) {
    if (!interrupt || !synchronizeRoutine) return FALSE;
    std::shared_ptr<InterruptRecord> record;
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        auto it = g_interrupts.find(interrupt);
        if (it == g_interrupts.end() || !it->second->connected) return FALSE;
        record = it->second;
    }
    std::lock_guard<std::mutex> lock(record->synchronization);
    return synchronizeRoutine(synchronizeContext);
}

void XboxKernelResetThreadExports() {
    {
        std::lock_guard<std::mutex> lock(g_apcMutex);
        g_apcs.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        g_interrupts.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
        g_systemInterrupts = {};
    }
}


namespace {
[[noreturn]] void RaiseUnsupportedHardwareImport(ULONG ordinal) {
    ULONG_PTR parameter = ordinal;
    RaiseException(0xC000001Du, EXCEPTION_NONCONTINUABLE, 1, &parameter);
}
}
extern "C" void __stdcall K_HalDisableSystemInterrupt(ULONG busInterruptLevel) {
    if (busInterruptLevel >= g_systemInterrupts.size()) return;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    g_systemInterrupts[busInterruptLevel].enabled = false;
}
extern "C" void __stdcall K_HalEnableSystemInterrupt(
    ULONG busInterruptLevel, ULONG interruptMode) {
    if (busInterruptLevel >= g_systemInterrupts.size()) return;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    auto& record = g_systemInterrupts[busInterruptLevel];
    record.mode = interruptMode;
    record.enabled = true;
}
extern "C" ULONG __stdcall K_HalGetInterruptVector(ULONG, UCHAR* irql) {
    if (irql) *irql = 0;
    return 0; // no physical interrupt routing table is available
}
extern "C" void __stdcall K_HalReadWritePCISpace(
    ULONG, ULONG, ULONG, void*, ULONG, BOOLEAN) {
    RaiseUnsupportedHardwareImport(46);
}
extern "C" LONG __stdcall K_KeSaveFloatingPointState(void*) {
    return static_cast<LONG>(0xC00000BBu); // STATUS_NOT_SUPPORTED
}
extern "C" LONG __stdcall K_KeRestoreFloatingPointState(void*) {
    return static_cast<LONG>(0xC00000BBu); // STATUS_NOT_SUPPORTED
}

UCHAR XboxKernelGetPendingSoftwareIrql() { return g_pendingSoftwareIrql; }

BOOLEAN XboxKernelIsSystemInterruptEnabled(ULONG level) {
    if (level >= g_systemInterrupts.size()) return FALSE;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    return g_systemInterrupts[level].enabled ? TRUE : FALSE;
}
ULONG XboxKernelGetSystemInterruptMode(ULONG level) {
    if (level >= g_systemInterrupts.size()) return 0;
    std::lock_guard<std::mutex> lock(g_systemInterruptMutex);
    return g_systemInterrupts[level].mode;
}
