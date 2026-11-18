#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "kernel_memory.h"
#include "kernel_object_manager.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {
constexpr LONG kSuccess = 0;
constexpr LONG kInvalidHandle = static_cast<LONG>(0xC0000008u);
constexpr LONG kInvalidParameter = static_cast<LONG>(0xC000000Du);
constexpr LONG kNoMemory = static_cast<LONG>(0xC0000017u);
constexpr LONG kAccessDenied = static_cast<LONG>(0xC0000022u);
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
    default: return kUnsuccessful;
    }
}

bool ValidHandle(HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}
using NtAllocateVirtualMemoryFn = LONG (NTAPI*)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
using NtFreeVirtualMemoryFn = LONG (NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG);
using NtProtectVirtualMemoryFn = LONG (NTAPI*)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);
using NtQueryVirtualMemoryFn = LONG (NTAPI*)(HANDLE, PVOID, ULONG, PVOID, SIZE_T, PSIZE_T);
template <typename T> T ResolveNtMemoryService(const char* name) noexcept {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? reinterpret_cast<T>(GetProcAddress(ntdll, name)) : nullptr;
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
    static const NtAllocateVirtualMemoryFn ntAllocate =
        ResolveNtMemoryService<NtAllocateVirtualMemoryFn>("NtAllocateVirtualMemory");
    void* allocation = requestedBase;
    SIZE_T nativeSize = alignedSize;
    LONG nativeStatus = ntAllocate
        ? ntAllocate(GetCurrentProcess(), &allocation, 0, &nativeSize,
                     allocationType, protect)
        : kUnsuccessful;
    if (!ntAllocate) {
        allocation = VirtualAlloc(requestedBase, alignedSize, allocationType, protect);
        if (!allocation) return StatusFromWin32(GetLastError());
    } else if (nativeStatus < 0) {
        return nativeStatus;
    }
    alignedSize = static_cast<std::uint32_t>(nativeSize);
    const std::uintptr_t actual = reinterpret_cast<std::uintptr_t>(allocation);
    if (actual > 0xFFFFFFFFu) {
        if (ntAllocate) { SIZE_T releaseSize = 0; (void)ntAllocate; }
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
    static const NtFreeVirtualMemoryFn ntFree =
        ResolveNtMemoryService<NtFreeVirtualMemoryFn>("NtFreeVirtualMemory");
    if (ntFree) {
        SIZE_T nativeSize = size;
        const LONG status = ntFree(GetCurrentProcess(), &base, &nativeSize, freeType);
        if (status < 0) return status;
    } else if (!VirtualFree(base, size, freeType)) {
        return StatusFromWin32(GetLastError());
    }
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
    static const NtProtectVirtualMemoryFn ntProtect =
        ResolveNtMemoryService<NtProtectVirtualMemoryFn>("NtProtectVirtualMemory");
    if (ntProtect) {
        SIZE_T nativeSize = alignedSize;
        const LONG status = ntProtect(GetCurrentProcess(), &base, &nativeSize,
                                      newProtect, &previous);
        if (status < 0) return status;
        alignedSize = static_cast<std::uint32_t>(nativeSize);
    } else if (!VirtualProtect(base, alignedSize, newProtect, &previous)) {
        return StatusFromWin32(GetLastError());
    }
    *baseAddress = base;
    *regionSize = alignedSize;
    *oldProtect = previous;
    return kSuccess;
}

extern "C" LONG __stdcall K_NtQueryVirtualMemory(void* baseAddress,
                                                  void* memoryInformation) {
    if (!baseAddress || !memoryInformation) return kInvalidParameter;
    MEMORY_BASIC_INFORMATION info{};
    static const NtQueryVirtualMemoryFn ntQuery =
        ResolveNtMemoryService<NtQueryVirtualMemoryFn>("NtQueryVirtualMemory");
    if (ntQuery) {
        SIZE_T returned = 0;
        const LONG status = ntQuery(GetCurrentProcess(), baseAddress, 0,
                                    &info, sizeof(info), &returned);
        if (status < 0) return status;
        if (returned < sizeof(info)) return kUnsuccessful;
    } else if (!VirtualQuery(baseAddress, &info, sizeof(info))) {
        return StatusFromWin32(GetLastError());
    }
    return CopyToGuest(memoryInformation, &info, sizeof(info))
        ? kSuccess : kInvalidParameter;
}

extern "C" LONG __stdcall K_NtDuplicateObject(HANDLE source, HANDLE* target,
                                                ULONG options) {
    if (!ValidHandle(source)) return kInvalidHandle;
    if (!target || (options & ~1u) != 0) return kInvalidParameter;
    if (XboxKernelIsObjectHandle(source)) {
        HANDLE duplicate = nullptr;
        LONG status = XboxKernelDuplicateObjectHandle(source, &duplicate, 0x2u);
        if (status != kSuccess) return status;
        if (!CopyToGuest(target, &duplicate, sizeof(duplicate))) {
            XboxKernelCloseObjectHandle(duplicate);
            return kInvalidParameter;
        }
        if ((options & 1u) != 0 && !XboxKernelCloseObjectHandle(source)) {
            XboxKernelCloseObjectHandle(duplicate);
            return kInvalidHandle;
        }
        return kSuccess;
    }
    const bool closeSource = (options & 1u) != 0; // NXDK DUPLICATE_CLOSE_SOURCE
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                         &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        const DWORD error = GetLastError();
        if (closeSource) CloseHandle(source);
        return StatusFromWin32(error);
    }
    if (!CopyToGuest(target, &duplicate, sizeof(duplicate))) {
        CloseHandle(duplicate);
        if (closeSource) CloseHandle(source);
        return kInvalidParameter;
    }
    if (closeSource && !CloseHandle(source)) {
        const DWORD error = GetLastError();
        CloseHandle(duplicate);
        return StatusFromWin32(error);
    }
    return kSuccess;
}




// Original Xbox kernel-stack and page-residency exports.
// StackBase is the high address returned to the caller; StackLimit is the
// low allocation address supplied back to MmDeleteKernelStack.

namespace {
struct GuestKernelStackAllocation {
    void* low = nullptr;
    SIZE_T bytes = 0;
};
std::mutex g_kernelStackMutex;
std::unordered_map<void*, GuestKernelStackAllocation> g_kernelStacks;

bool GuestRangeFits32(const void* base, SIZE_T bytes) noexcept {
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base);
    return base && bytes && start <= 0xFFFFFFFFu &&
        bytes <= 0x100000000ull - start;
}
}

extern "C" void* __stdcall K_MmCreateKernelStack(SIZE_T numberOfBytes,
                                                   BOOLEAN debuggerThread) {
    (void)debuggerThread; // Debugger-specific stack policy is not exposed by Win32.
    if (!numberOfBytes || numberOfBytes > 0x10000000u) return nullptr;
    constexpr SIZE_T page = 0x1000u;
    if (numberOfBytes > SIZE_MAX - (page - 1u)) return nullptr;
    const SIZE_T rounded = (numberOfBytes + page - 1u) & ~(page - 1u);
    void* low = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT,
                             PAGE_READWRITE);
    if (!low || !GuestRangeFits32(low, rounded)) {
        if (low) VirtualFree(low, 0, MEM_RELEASE);
        return nullptr;
    }
    void* stackBase = static_cast<std::uint8_t*>(low) + numberOfBytes;
    {
        std::lock_guard<std::mutex> lock(g_kernelStackMutex);
        if (!g_kernelStacks.emplace(stackBase,
                GuestKernelStackAllocation{low, rounded}).second) {
            VirtualFree(low, 0, MEM_RELEASE);
            return nullptr;
        }
    }
    return stackBase;
}

extern "C" void __stdcall K_MmDeleteKernelStack(void* stackBase,
                                                   void* stackLimit) {
    if (!stackBase || !stackLimit) return;
    void* allocation = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_kernelStackMutex);
        auto it = g_kernelStacks.find(stackBase);
        if (it == g_kernelStacks.end() || it->second.low != stackLimit) return;
        allocation = it->second.low;
        g_kernelStacks.erase(it);
    }
    VirtualFree(allocation, 0, MEM_RELEASE);
}

extern "C" void __stdcall K_MmLockUnlockBufferPages(
    void* baseAddress, SIZE_T numberOfBytes, BOOLEAN unlockPages) {
    if (!GuestRangeFits32(baseAddress, numberOfBytes)) return;
    if (unlockPages) {
        (void)VirtualUnlock(baseAddress, numberOfBytes);
    } else {
        (void)VirtualLock(baseAddress, numberOfBytes);
    }
}

extern "C" void __stdcall K_MmPersistContiguousMemory(
    void* baseAddress, SIZE_T numberOfBytes, BOOLEAN persist) {
    if (!GuestRangeFits32(baseAddress, numberOfBytes)) return;
    if (persist) {
        (void)VirtualLock(baseAddress, numberOfBytes);
    } else {
        (void)VirtualUnlock(baseAddress, numberOfBytes);
    }
}

// These operations require physical RAM / chipset mappings which the current
// Windows-backed guest does not own. Returning failure (or raising the normal
// unsupported-import exception for void APIs) is safer than inventing MMIO.
extern "C" void* __stdcall K_MmClaimGpuInstanceMemory(
    SIZE_T numberOfBytes, SIZE_T* numberOfPaddingBytes) {
    (void)numberOfBytes;
    if (numberOfPaddingBytes) *numberOfPaddingBytes = 0;
    return nullptr;
}
extern "C" void* __stdcall K_MmMapIoSpace(
    ULONG_PTR physicalAddress, SIZE_T numberOfBytes, ULONG protect) {
    (void)physicalAddress; (void)numberOfBytes; (void)protect;
    return nullptr;
}
extern "C" void* __stdcall K_MmUnmapIoSpace(
    void* baseAddress, SIZE_T numberOfBytes) {
    (void)baseAddress; (void)numberOfBytes;
    return nullptr;
}
extern "C" LONG __stdcall K_MmQueryStatistics(void* memoryStatistics) {
    (void)memoryStatistics;
    return static_cast<LONG>(0xC00000BBu); // STATUS_NOT_SUPPORTED
}
extern "C" void __stdcall K_MmLockUnlockPhysicalPage(
    ULONG_PTR physicalAddress, BOOLEAN unlockPage) {
    (void)physicalAddress; (void)unlockPage;
    const ULONG_PTR ordinal = 176;
    RaiseException(0xC000001Du, EXCEPTION_NONCONTINUABLE, 1, &ordinal);
}
