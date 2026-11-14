#include "../guest32/pci_compat.h"

#include <cstdint>
#include <cstdio>

int main() {
    std::uint32_t fsbRatio = 0;
    K_HalReadWritePCISpace(0, 0x60, 0x6C, &fsbRatio,
                          sizeof(fsbRatio), FALSE);
    if (fsbRatio != 0x00000801u) {
        std::fprintf(stderr,
            "FAIL: NXDK host-bridge FSB ratio expected 0x00000801, got 0x%08X\n",
            fsbRatio);
        return 1;
    }

    std::uint8_t divider = 0;
    std::uint8_t multiplier = 0;
    K_HalReadWritePCISpace(0, 0x60, 0x6C, &divider, 1, FALSE);
    K_HalReadWritePCISpace(0, 0x60, 0x6D, &multiplier, 1, FALSE);
    if (divider != 1 || multiplier != 8) {
        std::fprintf(stderr,
            "FAIL: PCI byte-lane read expected divider=1 multiplier=8, got %u/%u\n",
            divider, multiplier);
        return 2;
    }

    std::uint32_t absent = 0;
    K_HalReadWritePCISpace(0, 0x00, 0x00, &absent, sizeof(absent), FALSE);
    if (absent != 0xFFFFFFFFu) {
        std::fprintf(stderr,
            "FAIL: absent PCI function should read as all ones, got 0x%08X\n",
            absent);
        return 3;
    }

    std::uint32_t absentBus = 0;
    K_HalReadWritePCISpace(1, 0x60, 0x6C, &absentBus,
                           sizeof(absentBus), FALSE);
    if (absentBus != 0xFFFFFFFFu) {
        std::fprintf(stderr,
            "FAIL: absent PCI bus should read as all ones, got 0x%08X\n",
            absentBus);
        return 4;
    }

    std::puts("PASS: Xbox host-bridge PCI FSB probe and absent-device semantics.");
    return 0;
}
