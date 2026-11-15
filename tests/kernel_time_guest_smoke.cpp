#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "kernel_time.h"

namespace {
int Fail(const char* label, LONG status=0) {
    std::fprintf(stderr, "kernel_time_guest_smoke: FAIL: %s status=0x%08lX\n", label,
                 static_cast<unsigned long>(status));
    return 1;
}
LONGLONG HostNow() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{}; value.LowPart=ft.dwLowDateTime; value.HighPart=ft.dwHighDateTime;
    return static_cast<LONGLONG>(value.QuadPart);
}
bool CloseEnough(LONGLONG a, LONGLONG b, LONGLONG tolerance) {
    const LONGLONG delta=a>b ? a-b : b-a;
    return delta<=tolerance;
}
}
int main() {
    static_assert(sizeof(void*)==4, "time API ABI test must be x86");
    LARGE_INTEGER before{}, previous{}, after{};
    K_KeQuerySystemTime(&before);
    const LONGLONG hostBefore=HostNow();

    LARGE_INTEGER requested{};
    requested.QuadPart=132537600000000000LL; // 2012-01-01T00:00:00Z in FILETIME units
    LONG status=K_NtSetSystemTime(&requested, &previous);
    if (status!=0) return Fail("set virtual guest time",status);
    K_KeQuerySystemTime(&after);
    if (after.QuadPart<requested.QuadPart ||
        !CloseEnough(previous.QuadPart,before.QuadPart,20000000LL))
        return Fail("previous/current guest time values");
    if (!CloseEnough(after.QuadPart,requested.QuadPart,20000000LL))
        return Fail("guest time offset applied");

    LARGE_INTEGER negative{}; negative.QuadPart=-1;
    status=K_NtSetSystemTime(&negative,nullptr);
    if (status!=static_cast<LONG>(0xC000000Du)) return Fail("negative time rejected",status);
    LARGE_INTEGER unchanged{}; K_KeQuerySystemTime(&unchanged);
    if (!CloseEnough(unchanged.QuadPart,after.QuadPart,20000000LL))
        return Fail("invalid set leaves guest time unchanged");

    status=K_NtSetSystemTime(nullptr,nullptr);
    if (status!=static_cast<LONG>(0xC0000005u)) return Fail("invalid input pointer rejected",status);
    status=K_NtSetSystemTime(&requested,reinterpret_cast<LARGE_INTEGER*>(1));
    if (status!=static_cast<LONG>(0xC0000005u)) return Fail("invalid output pointer rejected",status);

    const LONGLONG hostAfter=HostNow();
    if (!CloseEnough(hostAfter,hostBefore,20000000LL))
        return Fail("host Windows clock was not modified");

    XboxKernelResetSystemTime();
    LARGE_INTEGER reset{}; K_KeQuerySystemTime(&reset);
    if (!CloseEnough(reset.QuadPart,HostNow(),20000000LL))
        return Fail("title shutdown/reset restores host-based guest time");
    std::puts("kernel_time_guest_smoke: PASS (x86 stdcall, virtual time set/query, validation, host clock isolation)");
    return 0;
}
