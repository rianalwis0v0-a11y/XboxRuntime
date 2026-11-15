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
LONG __stdcall K_NtDuplicateObject(HANDLE source, HANDLE* target, ULONG options);
}


extern "C" {
void* __stdcall K_MmClaimGpuInstanceMemory(SIZE_T numberOfBytes,
                                            SIZE_T* numberOfPaddingBytes);
void* __stdcall K_MmCreateKernelStack(SIZE_T numberOfBytes,
                                      BOOLEAN debuggerThread);
void __stdcall K_MmDeleteKernelStack(void* stackBase, void* stackLimit);
void __stdcall K_MmLockUnlockBufferPages(void* baseAddress,
                                         SIZE_T numberOfBytes,
                                         BOOLEAN unlockPages);
void __stdcall K_MmLockUnlockPhysicalPage(ULONG_PTR physicalAddress,
                                          BOOLEAN unlockPage);
void* __stdcall K_MmMapIoSpace(ULONG_PTR physicalAddress,
                               SIZE_T numberOfBytes, ULONG protect);
void __stdcall K_MmPersistContiguousMemory(void* baseAddress,
                                           SIZE_T numberOfBytes,
                                           BOOLEAN persist);
LONG __stdcall K_MmQueryStatistics(void* memoryStatistics);
void* __stdcall K_MmUnmapIoSpace(void* baseAddress, SIZE_T numberOfBytes);
}
