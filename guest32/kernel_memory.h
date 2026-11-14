#pragma once
#include <windows.h>
#include <cstdint>

// Pure 32-bit guest-address range normalizer shared by the guest implementation
// and x64 host-side tests. Returns false for zero-sized or overflowing ranges.
bool XboxKernelNormalizeGuestRange32(std::uint32_t base, std::uint32_t size,
                                     std::uint32_t* alignedBase,
                                     std::uint32_t* alignedSize);

extern "C" {
LONG __stdcall K_NtAllocateVirtualMemory(void** base, ULONG_PTR zeroBits,
                                         SIZE_T* regionSize, ULONG allocationType,
                                         ULONG protect);
LONG __stdcall K_NtFreeVirtualMemory(void** base, SIZE_T* regionSize,
                                     ULONG freeType);
LONG __stdcall K_NtProtectVirtualMemory(void** base, SIZE_T* regionSize,
                                        ULONG newProtect, ULONG* oldProtect);
LONG __stdcall K_NtQueryVirtualMemory(void* base, void* memoryInformation);
LONG __stdcall K_NtCreateEvent(HANDLE* out, void* objectAttributes,
                               ULONG eventType, BOOLEAN initialState);
LONG __stdcall K_NtCreateSemaphore(HANDLE* out, void* objectAttributes,
                                   LONG initialCount, LONG maximumCount);
LONG __stdcall K_NtCreateMutant(HANDLE* out, void* objectAttributes,
                                BOOLEAN initialOwner);
LONG __stdcall K_NtDuplicateObject(HANDLE source, HANDLE* target, ULONG options);
LONG __stdcall K_NtQueryEvent(HANDLE event, void* information);
LONG __stdcall K_NtQueryMutant(HANDLE mutant, void* information);
LONG __stdcall K_NtQuerySemaphore(HANDLE semaphore, void* information);
LONG __stdcall K_NtQueryTimer(HANDLE timer, void* information);
}
