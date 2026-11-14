#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

extern "C" LONG __stdcall K_NtDuplicateObject(HANDLE source, HANDLE* target,
                                                ULONG options) {
    if (!ValidHandle(source) || !target || (options & ~1u) != 0)
        return kInvalidParameter;
    const bool closeSource = (options & 1u) != 0; // NXDK DUPLICATE_CLOSE_SOURCE
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                         &duplicate, 0, FALSE, 0)) {
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

