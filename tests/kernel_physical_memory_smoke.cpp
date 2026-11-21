#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "kernel_physical_memory.h"

int main() {
    void* view = VirtualAlloc(nullptr, XBOX_GUEST_PHYSICAL_RAM_SIZE,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!view) return 1;
    bool ok = XboxKernelPhysicalMemoryInitialize(
        view, XBOX_GUEST_PHYSICAL_RAM_BASE, XBOX_GUEST_PHYSICAL_RAM_SIZE) != FALSE;
    std::uint32_t physical = 0xFFFFFFFFu;
    auto* bytes = static_cast<std::uint8_t*>(view);
    ok = ok && XboxKernelTranslateGuestVirtualToPhysical(
        bytes + 0x1234, 16, XboxGuestMemoryRead | XboxGuestMemoryWrite, &physical);
    ok = ok && physical == 0x1234u;
    ok = ok && !XboxKernelTranslateGuestVirtualToPhysical(
        bytes + XBOX_GUEST_PHYSICAL_RAM_SIZE - 8, 16,
        XboxGuestMemoryRead, &physical);
    ok = ok && !XboxKernelTranslateGuestVirtualToPhysical(
        bytes, 0, XboxGuestMemoryRead, &physical);
    std::uint32_t base = 0, size = 0;
    ok = ok && XboxKernelGetPhysicalMemoryView(&base, &size) == view &&
         base == 0 && size == XBOX_GUEST_PHYSICAL_RAM_SIZE;
    std::uint32_t allocatedPhysical = 0xFFFFFFFFu;
    void* allocation = XboxKernelAllocatePhysicalMemory(
        0x2000u, 0u, 0x03FFFFFFu, 0x1000u, &allocatedPhysical);
    ok = ok && allocation != nullptr && allocatedPhysical < XBOX_GUEST_PHYSICAL_RAM_SIZE &&
         (allocatedPhysical & 0xFFFu) == 0;
    std::uint32_t translated = 0xFFFFFFFFu;
    ok = ok && XboxKernelTranslateGuestVirtualToPhysical(
        allocation, 0x2000u, XboxGuestMemoryRead | XboxGuestMemoryWrite, &translated) &&
         translated == allocatedPhysical;
    if (allocation) {
        static_cast<std::uint8_t*>(allocation)[0] = 0xA5;
        ok = ok && bytes[allocatedPhysical] == 0xA5;
        ok = ok && XboxKernelFreePhysicalMemory(allocation);
        ok = ok && !XboxKernelFreePhysicalMemory(allocation);
    }
    DWORD oldProtection = 0;
    ok = ok && VirtualProtect(bytes + 0x3000, 0x1000, PAGE_READONLY, &oldProtection);
    ok = ok && XboxKernelTranslateGuestVirtualToPhysical(
        bytes + 0x3000, 4, XboxGuestMemoryRead, &physical);
    ok = ok && !XboxKernelTranslateGuestVirtualToPhysical(
        bytes + 0x3000, 4, XboxGuestMemoryWrite, &physical);
    DWORD ignoredProtection = 0;
    VirtualProtect(bytes + 0x3000, 0x1000, oldProtection, &ignoredProtection);
    XboxKernelPhysicalMemoryShutdown();
    ok = ok && !XboxKernelTranslateGuestVirtualToPhysical(
        bytes, 4, XboxGuestMemoryRead, &physical);
    VirtualFree(view, 0, MEM_RELEASE);
    if (!ok) {
        std::puts("kernel_physical_memory_smoke: FAIL");
        return 2;
    }
    std::puts("kernel_physical_memory_smoke: PASS");
    return 0;
}
