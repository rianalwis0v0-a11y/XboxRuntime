#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kernel_memory.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr LONG kSuccess = 0;
constexpr LONG kInvalidHandle = static_cast<LONG>(0xC0000008u);
constexpr LONG kInvalidParameter = static_cast<LONG>(0xC000000Du);
constexpr LONG kNoMemory = static_cast<LONG>(0xC0000017u);
constexpr LONG kAccessDenied = static_cast<LONG>(0xC0000022u);
constexpr LONG kNotSupported = static_cast<LONG>(0xC00000BBu);
constexpr LONG kUnsuccessful = static_cast<LONG>(0xC0000001u);
constexpr std::uint64_t kGuestAddressLimit = 0x100000000ull;
constexpr std::uint32_t kPageSize = 0x1000u;

LONG StatusFromWin32(DWORD error) noexcept {
    switch (error) {
    case ERROR_SUCCESS: return kSuccess;
    case ERROR_INVALID_HANDLE: return kInvalidHandle;
    case ERROR_INVALID_PARAMETER: return kInvalidParameter;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return kNoMemory;
    case ERROR_ACCESS_DENIED: return kAccessDenied;
    case ERROR_INVALID_ADDRESS: return static_cast<LONG>(0xC0000018u); // STATUS_CONFLICTING_ADDRESSES
    case ERROR_INVALID_PAGE_PROTECTION: return static_cast<LONG>(0xC0000045u);
    default: return kUnsuccessful;
    }
}

bool ValidHandle(HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

using NtQueryNativeFn = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
FARPROC NativeRoutine(const char* name) noexcept {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : nullptr;
}
LONG QueryNative(HANDLE handle, void* output, ULONG outputSize,
                 const char* routine) noexcept {
    if (!ValidHandle(handle)) return kInvalidHandle;
    if (!output) return kInvalidParameter;
    const FARPROC address = NativeRoutine(routine);
    if (!address) return static_cast<LONG>(0xC0000002u); // STATUS_NOT_IMPLEMENTED
    ULONG returned = 0;
    const auto query = reinterpret_cast<NtQueryNativeFn>(address);
    const LONG status = query(handle, 0, output, outputSize, &returned);
    if (status < 0) return status;
    return returned == 0 || returned <= outputSize ? status
        : static_cast<LONG>(0xC0000023u); // STATUS_BUFFER_TOO_SMALL
}

bool CopyToGuest(void* destination, const void* source, SIZE_T bytes) noexcept {
    if (!destination || !source || !bytes) return false;
    SIZE_T written = 0;
    return WriteProcessMemory(GetCurrentProcess(), destination, source, bytes, &written) &&
           written == bytes;
}
}

bool XboxKernelNormalizeGuestRange32(std::uint32_t base, std::uint32_t size,
                                     std::uint32_t* alignedBase,
                                     std::uint32_t* alignedSize) {
    if (!alignedBase || !alignedSize || !size) return false;
    const std::uint64_t start = std::uint64_t(base) & ~(std::uint64_t(kPageSize) - 1u);
    const std::uint64_t end = std::uint64_t(base) + size;
    if (end > kGuestAddressLimit) return false;
    const std::uint64_t roundedEnd =
        (end + kPageSize - 1u) & ~(std::uint64_t(kPageSize) - 1u);
    if (roundedEnd > kGuestAddressLimit || roundedEnd <= start ||
        roundedEnd - start > 0xFFFFFFFFull) return false;
    *alignedBase = static_cast<std::uint32_t>(start);
    *alignedSize = static_cast<std::uint32_t>(roundedEnd - start);
    return true;
}

extern "C" LONG __stdcall K_NtAllocateVirtualMemory(
    void** baseAddress, ULONG_PTR zeroBits, SIZE_T* regionSize,
    ULONG allocationType, ULONG protect) {
    if (!baseAddress || !regionSize || !*regionSize || *regionSize > 0xFFFFFFFFu || zeroBits != 0)
        return kInvalidParameter;
    constexpr ULONG allowed = MEM_RESERVE | MEM_COMMIT;
    if (!allocationType || (allocationType & ~allowed) != 0)
        return kInvalidParameter;

    const std::uintptr_t requested = reinterpret_cast<std::uintptr_t>(*baseAddress);
    if (requested > 0xFFFFFFFFu) return kInvalidParameter;
    std::uint32_t alignedBase = 0, alignedSize = 0;
    if (!XboxKernelNormalizeGuestRange32(static_cast<std::uint32_t>(requested),
                                         static_cast<std::uint32_t>(*regionSize),
                                         &alignedBase, &alignedSize))
        return kInvalidParameter;
    void* requestedBase = requested
        ? reinterpret_cast<void*>(static_cast<std::uintptr_t>(alignedBase)) : nullptr;
    void* allocation = VirtualAlloc(requestedBase, alignedSize, allocationType, protect);
    if (!allocation) return StatusFromWin32(GetLastError());
    const std::uintptr_t actual = reinterpret_cast<std::uintptr_t>(allocation);
    if (actual > 0xFFFFFFFFu) {
        VirtualFree(allocation, 0, MEM_RELEASE);
        return kInvalidParameter;
    }
    *baseAddress = allocation;
    *regionSize = alignedSize;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtFreeVirtualMemory(
    void** baseAddress, SIZE_T* regionSize, ULONG freeType) {
    if (!baseAddress || !*baseAddress || !regionSize ||
        (freeType != MEM_RELEASE && freeType != MEM_DECOMMIT))
        return kInvalidParameter;
    const std::uintptr_t requested = reinterpret_cast<std::uintptr_t>(*baseAddress);
    if (requested > 0xFFFFFFFFu) return kInvalidParameter;

    SIZE_T size = 0;
    void* base = *baseAddress;
    if (freeType == MEM_RELEASE) {
        if (*regionSize != 0) return kInvalidParameter;
    } else {
        std::uint32_t alignedBase = 0, alignedSize = 0;
        if (!regionSize || !*regionSize || *regionSize > 0xFFFFFFFFu ||
            !XboxKernelNormalizeGuestRange32(static_cast<std::uint32_t>(requested),
                static_cast<std::uint32_t>(*regionSize), &alignedBase, &alignedSize))
            return kInvalidParameter;
        base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(alignedBase));
        size = alignedSize;
    }
    if (!VirtualFree(base, size, freeType)) return StatusFromWin32(GetLastError());
    if (freeType == MEM_RELEASE) {
        *baseAddress = nullptr;
        *regionSize = 0;
    } else {
        *baseAddress = base;
        *regionSize = size;
    }
    return kSuccess;
}

extern "C" LONG __stdcall K_NtProtectVirtualMemory(
    void** baseAddress, SIZE_T* regionSize, ULONG newProtect, ULONG* oldProtect) {
    if (!baseAddress || !*baseAddress || !regionSize || !*regionSize || *regionSize > 0xFFFFFFFFu || !oldProtect)
        return kInvalidParameter;
    const std::uintptr_t requested = reinterpret_cast<std::uintptr_t>(*baseAddress);
    if (requested > 0xFFFFFFFFu) return kInvalidParameter;
    std::uint32_t alignedBase = 0, alignedSize = 0;
    if (!XboxKernelNormalizeGuestRange32(static_cast<std::uint32_t>(requested),
            static_cast<std::uint32_t>(*regionSize), &alignedBase, &alignedSize))
        return kInvalidParameter;
    void* base = reinterpret_cast<void*>(static_cast<std::uintptr_t>(alignedBase));
    DWORD previous = 0;
    if (!VirtualProtect(base, alignedSize, newProtect, &previous))
        return StatusFromWin32(GetLastError());
    *baseAddress = base;
    *regionSize = alignedSize;
    *oldProtect = previous;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtQueryVirtualMemory(void* baseAddress,
                                                  void* memoryInformation) {
    if (!baseAddress || !memoryInformation) return kInvalidParameter;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(baseAddress, &info, sizeof(info)))
        return StatusFromWin32(GetLastError());
    return CopyToGuest(memoryInformation, &info, sizeof(info))
        ? kSuccess : kInvalidParameter;
}

extern "C" LONG __stdcall K_NtCreateEvent(HANDLE* out, void* objectAttributes,
                                            ULONG eventType, BOOLEAN initialState) {
    if (!out || eventType > 1) return kInvalidParameter;
    if (objectAttributes) return kNotSupported; // Named object namespace is not implemented.
    HANDLE handle = CreateEventW(nullptr, eventType == 0, initialState != FALSE, nullptr);
    if (!handle) return StatusFromWin32(GetLastError());
    *out = handle;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtCreateSemaphore(HANDLE* out, void* objectAttributes,
                                                LONG initialCount, LONG maximumCount) {
    if (!out || objectAttributes || maximumCount <= 0 ||
        initialCount < 0 || initialCount > maximumCount)
        return objectAttributes ? kNotSupported : kInvalidParameter;
    HANDLE handle = CreateSemaphoreW(nullptr, initialCount, maximumCount, nullptr);
    if (!handle) return StatusFromWin32(GetLastError());
    *out = handle;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtCreateMutant(HANDLE* out, void* objectAttributes,
                                             BOOLEAN initialOwner) {
    if (!out) return kInvalidParameter;
    if (objectAttributes) return kNotSupported;
    HANDLE handle = CreateMutexW(nullptr, initialOwner != FALSE, nullptr);
    if (!handle) return StatusFromWin32(GetLastError());
    *out = handle;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtDuplicateObject(HANDLE source, HANDLE* target,
                                                ULONG options) {
    if (!ValidHandle(source) || !target || (options & ~1u) != 0)
        return kInvalidParameter;
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                         &duplicate, 0, FALSE, options & 1u))
        return StatusFromWin32(GetLastError());
    *target = duplicate;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtQueryEvent(HANDLE event, void* information) {
    struct EventBasicInformation { ULONG eventType; LONG eventState; };
    static_assert(sizeof(EventBasicInformation) == 8, "NXDK EVENT_BASIC_INFORMATION ABI");
    return QueryNative(event, information, sizeof(EventBasicInformation), "NtQueryEvent");
}
extern "C" LONG __stdcall K_NtQueryMutant(HANDLE mutant, void* information) {
    struct MutantBasicInformation { LONG currentCount; UCHAR ownedByCaller; UCHAR abandonedState; UCHAR padding[2]; };
    static_assert(sizeof(MutantBasicInformation) == 8, "NXDK MUTANT_BASIC_INFORMATION ABI");
    return QueryNative(mutant, information, sizeof(MutantBasicInformation), "NtQueryMutant");
}
extern "C" LONG __stdcall K_NtQuerySemaphore(HANDLE semaphore, void* information) {
    struct SemaphoreBasicInformation { LONG currentCount; LONG maximumCount; };
    static_assert(sizeof(SemaphoreBasicInformation) == 8, "NXDK SEMAPHORE_BASIC_INFORMATION ABI");
    return QueryNative(semaphore, information, sizeof(SemaphoreBasicInformation), "NtQuerySemaphore");
}
extern "C" LONG __stdcall K_NtQueryTimer(HANDLE timer, void* information) {
    struct TimerBasicInformation { LARGE_INTEGER remainingTime; BOOLEAN timerState; UCHAR padding[7]; };
    static_assert(sizeof(TimerBasicInformation) == 16, "NXDK TIMER_BASIC_INFORMATION ABI");
    return QueryNative(timer, information, sizeof(TimerBasicInformation), "NtQueryTimer");
}

extern "C" LONG __stdcall K_NtSetEvent(HANDLE event, LONG* previousState) {
    if (!ValidHandle(event)) return kInvalidHandle;
    const FARPROC address = NativeRoutine("NtSetEvent");
    if (!address) return kNotSupported;
    using SetEventNative = LONG (NTAPI*)(HANDLE, LONG*);
    return reinterpret_cast<SetEventNative>(address)(event, previousState);
}
extern "C" LONG __stdcall K_NtClearEvent(HANDLE event) {
    if (!ValidHandle(event)) return kInvalidHandle;
    const FARPROC address = NativeRoutine("NtClearEvent");
    if (!address) return kNotSupported;
    using ClearEventNative = LONG (NTAPI*)(HANDLE);
    return reinterpret_cast<ClearEventNative>(address)(event);
}
extern "C" LONG __stdcall K_NtWaitForSingleObject(
    HANDLE handle, BOOLEAN alertable, const LARGE_INTEGER* timeout) {
    if (!ValidHandle(handle)) return kInvalidHandle;
    DWORD milliseconds = INFINITE;
    if (timeout) {
        const LONGLONG ticks = timeout->QuadPart;
        if (ticks == 0) milliseconds = 0;
        else if (ticks < 0) {
            const std::uint64_t magnitude = std::uint64_t(-(ticks + 1)) + 1u;
            const std::uint64_t rounded = (magnitude + 9999u) / 10000u;
            milliseconds = rounded >= INFINITE ? INFINITE - 1u : static_cast<DWORD>(rounded);
        } else {
            FILETIME nowFileTime{};
            GetSystemTimeAsFileTime(&nowFileTime);
            ULARGE_INTEGER now{};
            now.LowPart = nowFileTime.dwLowDateTime;
            now.HighPart = nowFileTime.dwHighDateTime;
            const std::uint64_t deadline = static_cast<std::uint64_t>(ticks);
            if (deadline <= now.QuadPart) milliseconds = 0;
            else {
                const std::uint64_t rounded = (deadline - now.QuadPart + 9999u) / 10000u;
                milliseconds = rounded >= INFINITE ? INFINITE - 1u : static_cast<DWORD>(rounded);
            }
        }
    }
    const DWORD result = WaitForSingleObjectEx(handle, milliseconds, alertable ? TRUE : FALSE);
    if (result == WAIT_OBJECT_0) return kSuccess;
    if (result == WAIT_TIMEOUT) return static_cast<LONG>(0x00000102u);
    if (result == WAIT_IO_COMPLETION) return static_cast<LONG>(0x000000C0u);
    return StatusFromWin32(GetLastError());
}
