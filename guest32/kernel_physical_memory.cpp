#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "kernel_physical_memory.h"
#include <cstdint>
#include <map>
#include <unordered_map>

namespace {
SRWLOCK g_physicalLock = SRWLOCK_INIT;
std::uint8_t* g_view = nullptr;
std::uint32_t g_base = 0;
std::uint32_t g_length = 0;
std::map<std::uint32_t, std::uint32_t> g_freeRanges;
std::unordered_map<void*, std::uint32_t> g_allocations;

bool RangeValid(const void* address, SIZE_T length, ULONG access) noexcept {
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
        if ((access & XboxGuestMemoryWrite) != 0 &&
            (protection == PAGE_READONLY || protection == PAGE_EXECUTE ||
             protection == PAGE_EXECUTE_READ || protection == PAGE_WRITECOPY ||
             protection == PAGE_EXECUTE_WRITECOPY))
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
        g_freeRanges.clear();
        g_allocations.clear();
        g_freeRanges.emplace(0u, length);
    }
    ReleaseSRWLockExclusive(&g_physicalLock);
    return available ? TRUE : FALSE;
}

extern "C" void __stdcall XboxKernelPhysicalMemoryShutdown() {
    AcquireSRWLockExclusive(&g_physicalLock);
    g_view = nullptr;
    g_base = 0;
    g_length = 0;
    g_freeRanges.clear();
    g_allocations.clear();
    ReleaseSRWLockExclusive(&g_physicalLock);
}

extern "C" BOOL __stdcall XboxKernelTranslateGuestVirtualToPhysical(
    const void* guestAddress, SIZE_T length, ULONG access,
    std::uint32_t* physicalAddress) {
    constexpr ULONG allowed = XboxGuestMemoryRead | XboxGuestMemoryWrite;
    if (!physicalAddress || !guestAddress || !length || !access ||
        (access & ~allowed) != 0)
        return FALSE;

    AcquireSRWLockShared(&g_physicalLock);
    if (!g_view || !RangeValid(guestAddress, length, access)) {
        ReleaseSRWLockShared(&g_physicalLock);
        return FALSE;
    }
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

extern "C" void* __stdcall XboxKernelAllocatePhysicalMemory(
    std::uint32_t bytes, std::uint32_t minimumAddress,
    std::uint32_t maximumAddress, std::uint32_t alignment,
    std::uint32_t* physicalAddress) {
    if (!bytes || !physicalAddress) return nullptr;
    if (!alignment) alignment = 0x1000u;
    if ((alignment & (alignment - 1u)) != 0) return nullptr;
    AcquireSRWLockExclusive(&g_physicalLock);
    if (!g_view || !g_length) {
        ReleaseSRWLockExclusive(&g_physicalLock);
        return nullptr;
    }
    const std::uint64_t minAddress = minimumAddress;
    const std::uint64_t maxExclusive = maximumAddress
        ? static_cast<std::uint64_t>(maximumAddress) + 1u : 0x100000000ull;
    if (minAddress >= maxExclusive || minAddress < g_base) {
        ReleaseSRWLockExclusive(&g_physicalLock);
        return nullptr;
    }
    for (auto it = g_freeRanges.begin(); it != g_freeRanges.end(); ++it) {
        const std::uint64_t start = it->first;
        const std::uint64_t size = it->second;
        const std::uint64_t candidatePhysical = g_base + start > minAddress
            ? g_base + start : minAddress;
        const std::uint64_t alignedPhysical = (candidatePhysical + alignment - 1u) &
            ~(static_cast<std::uint64_t>(alignment) - 1u);
        const std::uint64_t aligned = alignedPhysical - g_base;
        const std::uint64_t end = aligned + bytes;
        if (aligned < start || end < aligned || end > start + size ||
            end > g_length || alignedPhysical + bytes > maxExclusive)
            continue;
        const std::uint32_t oldStart = it->first;
        const std::uint32_t oldSize = it->second;
        g_freeRanges.erase(it);
        if (aligned > oldStart)
            g_freeRanges.emplace(oldStart, static_cast<std::uint32_t>(aligned - oldStart));
        const std::uint64_t oldEnd = static_cast<std::uint64_t>(oldStart) + oldSize;
        if (end < oldEnd)
            g_freeRanges.emplace(static_cast<std::uint32_t>(end),
                static_cast<std::uint32_t>(oldEnd - end));
        void* result = g_view + static_cast<std::uint32_t>(aligned);
        g_allocations.emplace(result, bytes);
        *physicalAddress = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(g_base) + aligned);
        ReleaseSRWLockExclusive(&g_physicalLock);
        return result;
    }
    ReleaseSRWLockExclusive(&g_physicalLock);
    return nullptr;
}

extern "C" BOOL __stdcall XboxKernelFreePhysicalMemory(void* allocation) {
    if (!allocation) return FALSE;
    AcquireSRWLockExclusive(&g_physicalLock);
    const auto found = g_allocations.find(allocation);
    if (found == g_allocations.end()) {
        ReleaseSRWLockExclusive(&g_physicalLock);
        return FALSE;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(allocation);
    const std::uintptr_t view = reinterpret_cast<std::uintptr_t>(g_view);
    const std::uint32_t offset = static_cast<std::uint32_t>(address - view);
    const std::uint32_t allocatedSize = found->second;
    std::uint32_t size = allocatedSize;
    g_allocations.erase(found);
    std::uint32_t mergedStart = offset;
    auto next = g_freeRanges.lower_bound(offset);
    if (next != g_freeRanges.begin()) {
        auto previous = std::prev(next);
        if (static_cast<std::uint64_t>(previous->first) + previous->second == offset) {
            mergedStart = previous->first;
            size += previous->second;
            g_freeRanges.erase(previous);
        }
    }
    next = g_freeRanges.lower_bound(offset);
    if (next != g_freeRanges.end() &&
        static_cast<std::uint64_t>(offset) + allocatedSize == next->first) {
        size += next->second;
        g_freeRanges.erase(next);
    }
    g_freeRanges.emplace(mergedStart, size);
    ReleaseSRWLockExclusive(&g_physicalLock);
    return TRUE;
}
