#include "../guest32/kernel_memory.h"
#include "../guest32/kernel_gpu_service_protocol.h"
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    static_assert(sizeof(void*) == 8, "Host-side range test must use x64");
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
    std::vector<std::uint8_t> gpuMemory(XBOX_GPU_SHARED_MEMORY_SIZE, 0);
    if (!XboxKernelGpuMemoryInitialize(gpuMemory.data(), gpuMemory.size())) {
        std::fprintf(stderr, "shared GPU memory initialization failed\n");
        return 5;
    }
    SIZE_T padding = 99;
    void* firstGpuAllocation = K_MmClaimGpuInstanceMemory(0x1001u, &padding);
    if (firstGpuAllocation != gpuMemory.data() + XBOX_GPU_SHARED_VRAM_SIZE ||
        padding != 0) {
        std::fprintf(stderr, "first GPU instance allocation was not RAMIN-backed\n");
        return 6;
    }
    void* secondGpuAllocation = K_MmClaimGpuInstanceMemory(0x1000u, &padding);
    if (secondGpuAllocation != gpuMemory.data() + XBOX_GPU_SHARED_VRAM_SIZE + 0x2000u ||
        padding != 0x0FFFu) {
        std::fprintf(stderr, "GPU instance allocation alignment/padding failed\n");
        return 7;
    }
    if (K_MmClaimGpuInstanceMemory(XBOX_GPU_SHARED_RAMIN_SIZE, nullptr) != nullptr) {
        std::fprintf(stderr, "GPU instance allocator accepted an out-of-range claim\n");
        return 8;
    }
    XboxKernelGpuMemoryShutdown();
    if (K_MmClaimGpuInstanceMemory(0x100u, nullptr) != nullptr) {
        std::fprintf(stderr, "GPU instance allocation succeeded after shutdown\n");
        return 9;
    }
    std::puts("kernel_memory_host_tests: PASS (guest ranges and shared GPU RAMIN allocator)");
    return 0;
}
