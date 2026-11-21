#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "kernel_physical_memory.h"
#include <cstdint>
#include <mutex>

namespace {
SRWLOCK g_physicalLock = SRWLOCK_INIT;
std::uint8_t* g_view = nullptr;
std::uint32_t g_base = 0;
std::uint32_t g_length = 0;

bool RangeValid(const void* address, SIZE_T length) noexcept {
    if (!address || !length) return false;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
    if (length > UINTPTR_MAX - start) return false;
    const std::uintptr_t end = start + length;
    std::uintptr_t cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
            (info.Protect & 0xFFu) == PAGE_NOACCESS)
            return false;
        const DWORD protection = info.Protect & 0xFFu;
        if ((protection == PAGE_READONLY || protection == PAGE_EXECUTE ||
             protection == PAGE_EXECUTE_READ) && false)
            return false;
        const std::uintptr_t regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (info.RegionSize > UINTPTR_MAX - regionBase) return false;
        const std::uintptr_t regionEnd = regionBase + info.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}
}

extern "C" BOOL __stdcall XboxKernelPhysicalMemoryInitialize(
    void* mappedView, std::uint32_t physicalBase, std::uint32_t length) {
    if (!mappedView || !length ||
        static_cast<std::uint64_t>(physicalBase) + length > 0x100000000ull)
        return FALSE;
    AcquireSRWLockExclusive(&g_physicalLock);
    const bool available = g_view == nullptr;
    if (available) {
        g_view = static_cast<std::uint8_t*>(mappedView);
        g_base = physicalBase;
        g_length = length;
    }
    ReleaseSRWLockExclusive(&g_physicalLock);
    return available ? TRUE : FALSE;
}

extern "C" void __stdcall XboxKernelPhysicalMemoryShutdown() {
    AcquireSRWLockExclusive(&g_physicalLock);
    g_view = nullptr;
    g_base = 0;
    g_length = 0;
    ReleaseSRWLockExclusive(&g_physicalLock);
}

extern "C" BOOL __stdcall XboxKernelTranslateGuestVirtualToPhysical(
    const void* guestAddress, SIZE_T length, ULONG access,
    std::uint32_t* physicalAddress) {
    constexpr ULONG allowed = XboxGuestMemoryRead | XboxGuestMemoryWrite;
    if (!physicalAddress || !guestAddress || !length || !access ||
        (access & ~allowed) != 0 || !RangeValid(guestAddress, length))
        return FALSE;

    AcquireSRWLockShared(&g_physicalLock);
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(guestAddress);
    const std::uintptr_t viewStart = reinterpret_cast<std::uintptr_t>(g_view);
    const std::uint64_t offset = start >= viewStart ? start - viewStart : UINT64_MAX;
    const bool inside = g_view && offset < g_length &&
        length <= static_cast<std::uint64_t>(g_length) - offset;
    if (inside) {
        const std::uint64_t translated = static_cast<std::uint64_t>(g_base) + offset;
        if (translated + length <= 0x100000000ull)
            *physicalAddress = static_cast<std::uint32_t>(translated);
        else {
            ReleaseSRWLockShared(&g_physicalLock);
            return FALSE;
        }
    }
    ReleaseSRWLockShared(&g_physicalLock);
    return inside ? TRUE : FALSE;
}

extern "C" void* __stdcall XboxKernelGetPhysicalMemoryView(
    std::uint32_t* physicalBase, std::uint32_t* length) {
    AcquireSRWLockShared(&g_physicalLock);
    void* view = g_view;
    if (physicalBase) *physicalBase = g_base;
    if (length) *length = g_length;
    ReleaseSRWLockShared(&g_physicalLock);
    return view;
}
