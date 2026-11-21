#include "thread_compat.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

namespace {
constexpr ULONG kMinimumXboxKernelStack = 0x3000u;
constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
constexpr LONG kStatusInsufficientResources = static_cast<LONG>(0xC000009Au);

struct GuestThreadContext {
    XboxGuestKThread thread{};
    void* tlsAllocation = nullptr;
    void* tlsData = nullptr;
    HANDLE nativeHandle = nullptr;
    DWORD threadId = 0;
    XboxGuestStartRoutine startRoutine = nullptr;
    PVOID startContext = nullptr;
    XboxGuestSystemRoutine systemRoutine = nullptr;

    ~GuestThreadContext() {
        if (tlsAllocation)
            HeapFree(GetProcessHeap(), 0, tlsAllocation);
    }
};

struct GuestThreadStartPayload {
    std::shared_ptr<GuestThreadContext> context;
};

thread_local XboxGuestKThread g_defaultThread{};
thread_local XboxGuestKThread* g_currentThread = nullptr;
std::mutex g_threadMutex;
std::unordered_map<XboxGuestKThread*, std::shared_ptr<GuestThreadContext>> g_guestThreads;

void InitializeGuestKThread(XboxGuestKThread* thread, void* tlsData) {
    std::memset(thread, 0, sizeof(*thread));
    thread->dispatcherHeader[0] = 6;
    thread->dispatcherHeader[2] = 0x44;
    auto* waitList = reinterpret_cast<PLIST_ENTRY>(thread->dispatcherHeader + 8);
    waitList->Flink = waitList;
    waitList->Blink = waitList;
    thread->mutantListHead.Flink = &thread->mutantListHead;
    thread->mutantListHead.Blink = &thread->mutantListHead;

    ULONG_PTR lowLimit = 0;
    ULONG_PTR highLimit = 0;
    GetCurrentThreadStackLimits(&lowLimit, &highLimit);
    thread->stackBase = reinterpret_cast<PVOID>(highLimit);
    thread->stackLimit = reinterpret_cast<PVOID>(lowLimit);
    thread->kernelStack = reinterpret_cast<PVOID>(highLimit);
    thread->tlsData = tlsData;
    thread->state = 2;
    thread->npxState = 0x0A;
    thread->priority = 8;
}

XboxGuestKThread* CurrentGuestKThread() {
    if (!g_currentThread) {
        InitializeGuestKThread(&g_defaultThread, nullptr);
        g_currentThread = &g_defaultThread;
    }
    return g_currentThread;
}

DWORD WINAPI GuestSystemThreadStart(void* parameter) {
    std::unique_ptr<GuestThreadStartPayload> payload(
        static_cast<GuestThreadStartPayload*>(parameter));
    const std::shared_ptr<GuestThreadContext> context = payload->context;

    InitializeGuestKThread(&context->thread, context->tlsData);
    g_currentThread = &context->thread;

    if (context->systemRoutine)
        context->systemRoutine(context->startRoutine, context->startContext);
    else if (context->startRoutine)
        context->startRoutine(context->startContext);

    g_currentThread = nullptr;
    return 0;
}

VOID NTAPI DefaultSystemRoutine(XboxGuestStartRoutine startRoutine, PVOID startContext) {
    if (startRoutine)
        startRoutine(startContext);
}
}

extern "C" XboxGuestKThread* NTAPI K_KeGetCurrentThread() {
    return CurrentGuestKThread();
}

void XboxKernelInitializeCurrentThread() {
    (void)CurrentGuestKThread();
}

HANDLE XboxKernelResolveThreadHandle(HANDLE thread) {
    XboxGuestKThread* current = CurrentGuestKThread();
    if (!thread || thread == reinterpret_cast<HANDLE>(current))
        return GetCurrentThread();

    std::lock_guard<std::mutex> lock(g_threadMutex);
    for (const auto& entry : g_guestThreads) {
        if (reinterpret_cast<HANDLE>(entry.first) == thread)
            return entry.second->nativeHandle;
    }
    return thread;
}

void XboxKernelForgetThreadHandle(HANDLE thread) {
    if (!thread)
        return;
    std::lock_guard<std::mutex> lock(g_threadMutex);
    for (auto it = g_guestThreads.begin(); it != g_guestThreads.end(); ++it) {
        if (it->second->nativeHandle == thread) {
            g_guestThreads.erase(it);
            return;
        }
    }
}

extern "C" LONG NTAPI K_PsCreateSystemThreadEx(
    PHANDLE threadHandle,
    SIZE_T threadExtensionSize,
    SIZE_T kernelStackSize,
    SIZE_T tlsDataSize,
    PHANDLE threadId,
    XboxGuestStartRoutine startRoutine,
    PVOID startContext,
    BOOLEAN createSuspended,
    BOOLEAN debuggerThread,
    XboxGuestSystemRoutine systemRoutine) {
    (void)threadExtensionSize;
    (void)debuggerThread;
    if (!threadHandle || !startRoutine)
        return kStatusInvalidParameter;
    *threadHandle = nullptr;
    if (threadId)
        *threadId = nullptr;

    std::shared_ptr<GuestThreadContext> context;
    try {
        context = std::make_shared<GuestThreadContext>();
    } catch (...) {
        return kStatusInsufficientResources;
    }

    context->startRoutine = startRoutine;
    context->startContext = startContext;
    context->systemRoutine = systemRoutine;

    if (tlsDataSize) {
        if (tlsDataSize > static_cast<SIZE_T>(0x7FFFFFF0u))
            return kStatusInsufficientResources;
        context->tlsAllocation = HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, tlsDataSize + 15u);
        if (!context->tlsAllocation)
            return kStatusInsufficientResources;

        const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(
            context->tlsAllocation);
        const std::uintptr_t alignedTls = (raw + sizeof(std::uint32_t) + 15u) &
                                          ~std::uintptr_t(15u);
        context->tlsData = reinterpret_cast<void*>(
            alignedTls - sizeof(std::uint32_t));
    }

    const SIZE_T stackSize = std::max<SIZE_T>(
        kernelStackSize, static_cast<SIZE_T>(kMinimumXboxKernelStack));
    auto* payload = new (std::nothrow) GuestThreadStartPayload{context};
    if (!payload)
        return kStatusInsufficientResources;

    DWORD nativeThreadId = 0;
    HANDLE nativeThread = CreateThread(
        nullptr, stackSize, GuestSystemThreadStart, payload,
        CREATE_SUSPENDED, &nativeThreadId);
    if (!nativeThread) {
        delete payload;
        return kStatusInsufficientResources;
    }

    context->nativeHandle = nativeThread;
    context->threadId = nativeThreadId;
    try {
        std::lock_guard<std::mutex> lock(g_threadMutex);
        g_guestThreads.emplace(&context->thread, context);
    } catch (...) {
        TerminateThread(nativeThread, ERROR_NOT_ENOUGH_MEMORY);
        WaitForSingleObject(nativeThread, INFINITE);
        delete payload;
        CloseHandle(nativeThread);
        return kStatusInsufficientResources;
    }

    *threadHandle = nativeThread;
    if (threadId) {
        *threadId = reinterpret_cast<HANDLE>(
            static_cast<std::uintptr_t>(nativeThreadId));
    }

    if (!createSuspended && ResumeThread(nativeThread) == DWORD(-1)) {
        XboxKernelForgetThreadHandle(nativeThread);
        *threadHandle = nullptr;
        if (threadId)
            *threadId = nullptr;
        TerminateThread(nativeThread, ERROR_GEN_FAILURE);
        WaitForSingleObject(nativeThread, INFINITE);
        delete payload;
        CloseHandle(nativeThread);
        return kStatusInsufficientResources;
    }

    return 0;
}

extern "C" LONG NTAPI K_PsCreateSystemThread(
    PHANDLE threadHandle,
    PHANDLE threadId,
    XboxGuestStartRoutine startRoutine,
    PVOID startContext,
    BOOLEAN debuggerThread) {
    return K_PsCreateSystemThreadEx(
        threadHandle, 0, kMinimumXboxKernelStack, 0, threadId,
        startRoutine, startContext, FALSE, debuggerThread, DefaultSystemRoutine);
}
