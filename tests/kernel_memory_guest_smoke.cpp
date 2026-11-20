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
    static_assert(sizeof(void*) == 4, "Xbox guest ABI test must be x86");
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


    // Group A: kernel-stack allocation follows the Xbox high-base/low-limit
    // convention and only releases an exact pair returned by the allocator.
    void* stackBase = K_MmCreateKernelStack(0x2801, FALSE);
    if (!stackBase) return Fail("MmCreateKernelStack", 0xC0000017L);
    MEMORY_BASIC_INFORMATION stackAllocation{};
    if (!VirtualQuery(static_cast<std::uint8_t*>(stackBase) - 1,
                      &stackAllocation, sizeof(stackAllocation)))
        return Fail("query kernel stack allocation", 0xC0000001L);
    void* stackLimit = stackAllocation.AllocationBase;
    if (reinterpret_cast<std::uintptr_t>(stackBase) > 0xFFFFFFFFu ||
        reinterpret_cast<std::uintptr_t>(stackLimit) > 0xFFFFFFFFu)
        return Fail("kernel stack must be guest-addressable", 0xC000000DL);
    K_MmDeleteKernelStack(stackBase, reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(stackLimit) + 0x1000));
    // A mismatched limit must not release the allocation.
    MEMORY_BASIC_INFORMATION stackInfo{};
    if (!VirtualQuery(stackLimit, &stackInfo, sizeof(stackInfo)) ||
        stackInfo.State != MEM_COMMIT)
        return Fail("reject mismatched MmDeleteKernelStack pair", 0xC0000001L);
    K_MmDeleteKernelStack(stackBase, stackLimit);
    if (VirtualQuery(stackLimit, &stackInfo, sizeof(stackInfo)) &&
        stackInfo.State == MEM_COMMIT)
        return Fail("MmDeleteKernelStack release", 0xC0000001L);

    // Residency calls are void exports; exercise both directions on a
    // committed guest buffer without assuming the host can pin every page.
    void* resident = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT,
                                  PAGE_READWRITE);
    if (!resident) return Fail("residency test allocation", 0xC0000017L);
    K_MmLockUnlockBufferPages(resident, 0x1000, FALSE);
    K_MmLockUnlockBufferPages(resident, 0x1000, TRUE);
    K_MmPersistContiguousMemory(resident, 0x1000, TRUE);
    K_MmPersistContiguousMemory(resident, 0x1000, FALSE);
    VirtualFree(resident, 0, MEM_RELEASE);

    SIZE_T padding = 99;
    if (K_MmClaimGpuInstanceMemory(0x1000, &padding) != nullptr || padding != 0)
        return Fail("GPU instance memory must fail without a GPU memory backend", 0xC0000001L);
    void* gpuRegisters = K_MmMapIoSpace(0xFD000000u, 0x1000, PAGE_READWRITE);
    if (gpuRegisters != reinterpret_cast<void*>(0xFD000000u))
        return Fail("NV2A MMIO aperture mapping", 0xC0000001L);
    if (K_MmUnmapIoSpace(gpuRegisters, 0x1000) != gpuRegisters)
        return Fail("NV2A MMIO aperture unmapping", 0xC0000001L);
    if (K_MmMapIoSpace(0xFE800000u, 0x1000, PAGE_READWRITE) != nullptr)
        return Fail("reject non-NV2A physical MMIO", 0xC0000001L);
    if (K_MmQueryStatistics(nullptr) != static_cast<LONG>(0xC00000BBu))
        return Fail("MmQueryStatistics explicit unsupported status", 0xC0000001L);

    std::puts("kernel_memory_guest_smoke: PASS (x86 ABI, VM + Group A stack/residency + NV2A MMIO aperture)");
    return 0;
}
