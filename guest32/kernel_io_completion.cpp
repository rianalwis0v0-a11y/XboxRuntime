#include "kernel_io_completion.h"
#include "kernel_object_manager.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

namespace {
struct GuestIoStatusBlock {
    union { LONG Status; void* Pointer; };
    ULONG Information;
};
static_assert(sizeof(GuestIoStatusBlock) == 8, "Xbox IO_STATUS_BLOCK x86 layout");

struct GuestIoCompletionPacket {
    OVERLAPPED overlapped{};
    void* keyContext = nullptr;
    void* apcContext = nullptr;
    LONG ioStatus = 0;
    ULONG_PTR information = 0;
};
struct GuestIoCompletionState {
    HANDLE port = nullptr;
    std::atomic<LONG> depth{0};
    ~GuestIoCompletionState() {
        if (!port) return;
        for (;;) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, 0);
            if (!overlapped) break;
            delete reinterpret_cast<GuestIoCompletionPacket*>(overlapped);
        }
        CloseHandle(port);
    }
};
std::mutex g_completionMutex;
std::unordered_map<void*, std::shared_ptr<GuestIoCompletionState>> g_completions;

void DestroyCompletionObject(void* raw) {
    std::lock_guard<std::mutex> lock(g_completionMutex);
    g_completions.erase(raw);
}
std::shared_ptr<GuestIoCompletionState> FindCompletionPayload(void* raw) {
    std::lock_guard<std::mutex> lock(g_completionMutex);
    auto it = g_completions.find(raw);
    return it == g_completions.end() ? nullptr : it->second;
}
LONG StatusFromWin32(DWORD error) {
    switch (error) {
    case ERROR_SUCCESS: return 0;
    case ERROR_INVALID_HANDLE: return LONG(0xC0000008u);
    case ERROR_INVALID_PARAMETER: return LONG(0xC000000Du);
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return LONG(0xC0000017u);
    case ERROR_ACCESS_DENIED: return LONG(0xC0000022u);
    case WAIT_TIMEOUT: return LONG(0x00000102u);
    default: return LONG(0xC0000001u);
    }
}
DWORD CompletionTimeout(const LARGE_INTEGER* timeout) {
    if (!timeout) return INFINITE;
    if (timeout->QuadPart == 0) return 0;
    if (timeout->QuadPart < 0) {
        const uint64_t ticks = static_cast<uint64_t>(-(timeout->QuadPart + 1)) + 1;
        const uint64_t ms = (ticks + 9999u) / 10000u;
        return ms >= INFINITE ? INFINITE - 1 : static_cast<DWORD>(ms);
    }
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER now{};
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    const uint64_t deadline = static_cast<uint64_t>(timeout->QuadPart);
    if (deadline <= now.QuadPart) return 0;
    const uint64_t ms = (deadline - now.QuadPart + 9999u) / 10000u;
    return ms >= INFINITE ? INFINITE - 1 : static_cast<DWORD>(ms);
}
std::shared_ptr<GuestIoCompletionState> AcquireCompletionHandle(
    HANDLE handle, LONG* status) {
    void* raw = nullptr;
    if (!XboxKernelAcquireObjectReference(handle, XboxKernelObjectType::IoCompletion, &raw)) {
        if (status) *status = static_cast<LONG>(XboxKernelGetLastObjectLookupStatus());
        return nullptr;
    }
    auto state = FindCompletionPayload(raw);
    XboxKernelReleaseObjectReference(raw);
    if (!state && status) *status = LONG(0xC0000008u);
    return state;
}
LONG PostCompletion(const std::shared_ptr<GuestIoCompletionState>& state,
                    void* keyContext, void* apcContext,
                    LONG ioStatus, ULONG_PTR information) {
    if (!state) return LONG(0xC0000008u);
    auto* packet = new (std::nothrow) GuestIoCompletionPacket{};
    if (!packet) return LONG(0xC0000017u);
    packet->keyContext = keyContext;
    packet->apcContext = apcContext;
    packet->ioStatus = ioStatus;
    packet->information = information;
    packet->overlapped.Internal = static_cast<ULONG_PTR>(static_cast<ULONG>(ioStatus));
    packet->overlapped.InternalHigh = information;
    state->depth.fetch_add(1, std::memory_order_acq_rel);
    if (!PostQueuedCompletionStatus(state->port,
            static_cast<DWORD>(information),
            reinterpret_cast<ULONG_PTR>(keyContext), &packet->overlapped)) {
        const DWORD error = GetLastError();
        state->depth.fetch_sub(1, std::memory_order_acq_rel);
        delete packet;
        return StatusFromWin32(error);
    }
    return 0;
}
}

extern "C" LONG __stdcall K_NtCreateIoCompletion(
    HANDLE* out, ACCESS_MASK, void* objectAttributes, ULONG count) {
    if (!out) return LONG(0xC000000Du);
    *out = nullptr;
    if (objectAttributes) return LONG(0xC00000BBu);
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, count);
    if (!port) return StatusFromWin32(GetLastError());
    std::shared_ptr<GuestIoCompletionState> state;
    try {
        state = std::make_shared<GuestIoCompletionState>();
        state->port = port;
        {
            std::lock_guard<std::mutex> lock(g_completionMutex);
            g_completions.emplace(state.get(), state);
        }
    } catch (...) {
        if (state) state->port = nullptr;
        CloseHandle(port);
        return LONG(0xC0000017u);
    }
    HANDLE token = XboxKernelCreateObjectHandle(
        state.get(), XboxKernelObjectType::IoCompletion, DestroyCompletionObject);
    if (!token) {
        DestroyCompletionObject(state.get());
        return LONG(0xC0000017u);
    }
    *out = token;
    return 0;
}

extern "C" LONG __stdcall K_NtSetIoCompletion(
    HANDLE handle, void* keyContext, void* apcContext,
    LONG ioStatus, ULONG_PTR information) {
    LONG status = 0;
    auto state = AcquireCompletionHandle(handle, &status);
    if (!state) return status;
    return PostCompletion(state, keyContext, apcContext, ioStatus, information);
}

extern "C" LONG __stdcall K_IoSetIoCompletion(
    void* ioCompletion, void* keyContext, void* apcContext,
    LONG ioStatus, ULONG_PTR information) {
    if (!ioCompletion) return LONG(0xC000000Du);
    return PostCompletion(FindCompletionPayload(ioCompletion), keyContext,
                          apcContext, ioStatus, information);
}

extern "C" LONG __stdcall K_NtRemoveIoCompletion(
    HANDLE handle, void** keyContext, void** apcContext,
    void* ioStatusBlock, const LARGE_INTEGER* timeout) {
    if (!keyContext || !apcContext || !ioStatusBlock)
        return LONG(0xC000000Du);
    LONG status = 0;
    auto state = AcquireCompletionHandle(handle, &status);
    if (!state) return status;
    DWORD bytes = 0;
    ULONG_PTR completionKey = 0;
    OVERLAPPED* overlapped = nullptr;
    const BOOL removed = GetQueuedCompletionStatus(
        state->port, &bytes, &completionKey, &overlapped,
        CompletionTimeout(timeout));
    if (!overlapped) return StatusFromWin32(removed ? ERROR_INVALID_DATA : GetLastError());
    auto* packet = reinterpret_cast<GuestIoCompletionPacket*>(overlapped);
    state->depth.fetch_sub(1, std::memory_order_acq_rel);
    auto* io = static_cast<GuestIoStatusBlock*>(ioStatusBlock);
    *keyContext = packet->keyContext;
    *apcContext = packet->apcContext;
    io->Status = packet->ioStatus;
    io->Information = static_cast<ULONG>(packet->information);
    delete packet;
    return 0;
}

extern "C" LONG __stdcall K_NtQueryIoCompletion(HANDLE handle, void* information) {
    if (!information) return LONG(0xC000000Du);
    LONG status = 0;
    auto state = AcquireCompletionHandle(handle, &status);
    if (!state) return status;
    struct GuestIoCompletionBasicInformation { LONG Depth; };
    auto* result = static_cast<GuestIoCompletionBasicInformation*>(information);
    const LONG depth = state->depth.load(std::memory_order_acquire);
    result->Depth = depth < 0 ? 0 : depth;
    return 0;
}
