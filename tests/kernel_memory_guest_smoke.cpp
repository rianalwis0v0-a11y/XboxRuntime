#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "kernel_memory.h"

namespace {
int Fail(const char* what, LONG status) {
    std::fprintf(stderr, "kernel_memory_guest_smoke: %s status=0x%08lX\n",
                 what, static_cast<unsigned long>(status));
    return 1;
}
}
int main() {
    void* base = nullptr;
    SIZE_T size = 0x1801;
    LONG status = K_NtAllocateVirtualMemory(&base, 0, &size,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (status != 0 || !base || size != 0x2000) return Fail("allocate/round", status);

    auto* bytes = static_cast<volatile std::uint8_t*>(base);
    bytes[0] = 0x5A;
    bytes[0x1FFF] = 0xA5;
    ULONG oldProtect = 0;
    void* protectBase = static_cast<std::uint8_t*>(base) + 0x1001;
    SIZE_T protectSize = 1;
    status = K_NtProtectVirtualMemory(&protectBase, &protectSize,
                                      PAGE_READONLY, &oldProtect);
    if (status != 0 || oldProtect != PAGE_READWRITE ||
        protectBase != static_cast<std::uint8_t*>(base) + 0x1000 ||
        protectSize != 0x1000) return Fail("protect/page rounding", status);

    MEMORY_BASIC_INFORMATION memory{};
    status = K_NtQueryVirtualMemory(protectBase, &memory);
    if (status != 0 || memory.State != MEM_COMMIT ||
        memory.Protect != PAGE_READONLY) return Fail("query committed/protection", status);

    void* decommitBase = static_cast<std::uint8_t*>(base) + 0x1000;
    SIZE_T decommitSize = 0x1000;
    status = K_NtFreeVirtualMemory(&decommitBase, &decommitSize, MEM_DECOMMIT);
    if (status != 0 || decommitBase != static_cast<std::uint8_t*>(base) + 0x1000 ||
        decommitSize != 0x1000) return Fail("decommit", status);
    status = K_NtQueryVirtualMemory(decommitBase, &memory);
    if (status != 0 || memory.State != MEM_RESERVE) return Fail("query reserved", status);

    void* releaseBase = base;
    SIZE_T releaseSize = 0;
    status = K_NtFreeVirtualMemory(&releaseBase, &releaseSize, MEM_RELEASE);
    if (status != 0 || releaseBase != nullptr || releaseSize != 0)
        return Fail("release", status);

    void* invalidBase = nullptr;
    SIZE_T invalidSize = 0x1000;
    status = K_NtAllocateVirtualMemory(&invalidBase, 1, &invalidSize,
                                       MEM_RESERVE, PAGE_READWRITE);
    if (status != static_cast<LONG>(0xC000000Du))
        return Fail("reject unsupported ZeroBits", status);

    std::puts("kernel_memory_guest_smoke: PASS (x86 stdcall ABI, VM reserve/commit/protect/query/decommit/release)");
    return 0;
}
