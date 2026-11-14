#include "../guest32/kernel_memory.h"
#include <cstdint>
#include <cstdio>

int main() {
    std::uint32_t base = 0, size = 0;
    if (!XboxKernelNormalizeGuestRange32(0x00001234u, 0x00001001u, &base, &size) ||
        base != 0x00001000u || size != 0x00002000u) {
        std::fprintf(stderr, "unaligned range normalization failed\n");
        return 1;
    }
    if (!XboxKernelNormalizeGuestRange32(0, 1, &base, &size) ||
        base != 0 || size != 0x1000u) {
        std::fprintf(stderr, "zero-base range normalization failed\n");
        return 2;
    }
    if (XboxKernelNormalizeGuestRange32(0xFFFFFFFFu, 2, &base, &size)) {
        std::fprintf(stderr, "overflowing 32-bit guest range accepted\n");
        return 3;
    }
    if (XboxKernelNormalizeGuestRange32(0, 0, &base, &size)) {
        std::fprintf(stderr, "zero-sized guest range accepted\n");
        return 4;
    }
    std::puts("kernel_memory_host_tests: PASS (32-bit range alignment and overflow boundaries)");
    return 0;
}
