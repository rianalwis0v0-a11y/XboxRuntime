#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include "kernel_time.h"

namespace {
std::atomic<LONGLONG> g_guestTimeOffset100ns{0};
std::mutex g_guestTimeSetMutex;

bool GuestSpanAccessible(const void* pointer, std::size_t length, bool write) {
    if (!length) return true;
    if (!pointer) return false;
    const std::uintptr_t start=reinterpret_cast<std::uintptr_t>(pointer);
    if (length > UINTPTR_MAX-start) return false;
    const std::uintptr_t end=start+length;
    std::uintptr_t cursor=start;
    while (cursor<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD|PAGE_NOACCESS)))
            return false;
        const DWORD p=mbi.Protect & 0xFFu;
        const bool readable=p==PAGE_READONLY || p==PAGE_READWRITE || p==PAGE_WRITECOPY ||
            p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
        const bool writable=p==PAGE_READWRITE || p==PAGE_WRITECOPY ||
            p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
        if (write ? !writable : !readable) return false;
        const std::uintptr_t region=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > UINTPTR_MAX-region) return false;
        const std::uintptr_t next=region+mbi.RegionSize;
        if (next<=cursor) return false;
        cursor=(std::min)(next,end);
    }
    return true;
}
LONGLONG HostSystemTime100ns() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart=ft.dwLowDateTime;
    value.HighPart=ft.dwHighDateTime;
    return static_cast<LONGLONG>(value.QuadPart);
}
LONGLONG AddOffsetSaturated(LONGLONG base, LONGLONG offset) {
    const LONGLONG max=(std::numeric_limits<LONGLONG>::max)();
    const LONGLONG min=(std::numeric_limits<LONGLONG>::min)();
    if (offset>0 && base>max-offset) return max;
    if (offset<0 && base<min-offset) return min;
    return base+offset;
}
}

std::int64_t XboxKernelCurrentSystemTime100ns() {
    return AddOffsetSaturated(HostSystemTime100ns(), g_guestTimeOffset100ns.load(std::memory_order_relaxed));
}
void XboxKernelResetSystemTime() {
    std::lock_guard<std::mutex> lock(g_guestTimeSetMutex);
    g_guestTimeOffset100ns.store(0, std::memory_order_release);
}
extern "C" LONG __stdcall K_NtSetSystemTime(LARGE_INTEGER* systemTime, LARGE_INTEGER* previousTime) {
    constexpr LONG kStatusSuccess=0;
    constexpr LONG kStatusAccessViolation=static_cast<LONG>(0xC0000005u);
    constexpr LONG kStatusInvalidParameter=static_cast<LONG>(0xC000000Du);
    if (!GuestSpanAccessible(systemTime, sizeof(LARGE_INTEGER), false) ||
        (previousTime && !GuestSpanAccessible(previousTime, sizeof(LARGE_INTEGER), true)))
        return kStatusAccessViolation;
    LARGE_INTEGER requested{};
    std::memcpy(&requested, systemTime, sizeof(requested));
    if (requested.QuadPart < 0) return kStatusInvalidParameter;

    std::lock_guard<std::mutex> lock(g_guestTimeSetMutex);
    const LONGLONG hostNow=HostSystemTime100ns();
    const LONGLONG current=AddOffsetSaturated(hostNow, g_guestTimeOffset100ns.load(std::memory_order_relaxed));
    if (previousTime) {
        LARGE_INTEGER oldValue{};
        oldValue.QuadPart=current;
        std::memcpy(previousTime, &oldValue, sizeof(oldValue));
    }
    g_guestTimeOffset100ns.store(requested.QuadPart-hostNow, std::memory_order_release);
    return kStatusSuccess;
}
#if !defined(XBOX_KERNEL_COMPAT_HAS_KEQUERYSYSTEMTIME)
extern "C" void __stdcall K_KeQuerySystemTime(LARGE_INTEGER* currentTime) {
    if (!GuestSpanAccessible(currentTime, sizeof(LARGE_INTEGER), true)) {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return;
    }
    LARGE_INTEGER value{};
    value.QuadPart=XboxKernelCurrentSystemTime100ns();
    std::memcpy(currentTime, &value, sizeof(value));
}

#endif // XBOX_KERNEL_COMPAT_HAS_KEQUERYSYSTEMTIME
