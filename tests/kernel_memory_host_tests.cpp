#include "../guest32/kernel_memory.h"
#include "../guest32/kernel_gpu_service_protocol.h"
#include <cstdint>
#include <cstdio>

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
    HANDLE sharedMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, XBOX_GPU_SHARED_MEMORY_SIZE, nullptr);
    if (!sharedMapping) {
        std::fprintf(stderr, "shared GPU file mapping creation failed\n");
        return 5;
    }
    auto* hostView = static_cast<std::uint8_t*>(MapViewOfFile(
        sharedMapping, FILE_MAP_ALL_ACCESS, 0, 0, XBOX_GPU_SHARED_MEMORY_SIZE));
    auto* guestView = static_cast<std::uint8_t*>(MapViewOfFile(
        sharedMapping, FILE_MAP_ALL_ACCESS, 0, 0, XBOX_GPU_SHARED_MEMORY_SIZE));
    if (!hostView || !guestView || hostView == guestView ||
        !XboxKernelGpuMemoryInitialize(guestView, XBOX_GPU_SHARED_MEMORY_SIZE)) {
        std::fprintf(stderr, "shared GPU memory views failed to initialize\n");
        return 6;
    }
    SIZE_T padding = 99;
    void* firstGpuAllocation = K_MmClaimGpuInstanceMemory(0x1001u, &padding);
    if (firstGpuAllocation != guestView + XBOX_GPU_SHARED_VRAM_SIZE ||
        padding != 0x0FFFu) {
        std::fprintf(stderr, "first GPU instance allocation was not RAMIN-backed\n");
        return 7;
    }
    static_cast<std::uint8_t*>(firstGpuAllocation)[0] = 0xA5u;
    if (hostView[XBOX_GPU_SHARED_VRAM_SIZE] != 0xA5u) {
        std::fprintf(stderr, "host and guest GPU memory views are not coherent\n");
        return 8;
    }
    void* dmaRead = XboxKernelResolveGpuPhysicalAddress(0x1234u, 16u, 1u);
    void* dmaWrite = XboxKernelResolveGpuPhysicalAddress(0x2000u, 16u, 2u);
    if (dmaRead != guestView + 0x1234u ||
        dmaWrite != guestView + 0x2000u) {
        std::fprintf(stderr, "GPU physical DMA ranges did not resolve into shared unified memory\\n");
        return 12;
    }
    static_cast<std::uint8_t*>(dmaWrite)[0] = 0x5Au;
    if (hostView[0x2000u] != 0x5Au) {
        std::fprintf(stderr, "DMA writes were not coherent with the host GPU view\\n");
        return 13;
    }
    if (XboxKernelResolveGpuPhysicalAddress(
            XBOX_GPU_SHARED_VRAM_SIZE - 8u, 16u, 1u) != nullptr ||
        XboxKernelResolveGpuPhysicalAddress(0u, 16u, 0u) != nullptr ||
        XboxKernelResolveGpuPhysicalAddress(0u, 0u, 1u) != nullptr) {
        std::fprintf(stderr, "GPU DMA resolver accepted invalid access/range\\n");
        return 14;
    }
    void* secondGpuAllocation = K_MmClaimGpuInstanceMemory(0x1000u, &padding);
    if (secondGpuAllocation != guestView + XBOX_GPU_SHARED_VRAM_SIZE + 0x2000u ||
        padding != 0) {
        std::fprintf(stderr, "GPU instance allocation alignment/padding failed\n");
        return 9;
    }
    if (K_MmClaimGpuInstanceMemory(XBOX_GPU_SHARED_RAMIN_SIZE, nullptr) != nullptr) {
        std::fprintf(stderr, "GPU instance allocator accepted an out-of-range claim\n");
        return 10;
    }
    XboxKernelGpuMemoryShutdown();
    if (K_MmClaimGpuInstanceMemory(0x100u, nullptr) != nullptr) {
        std::fprintf(stderr, "GPU instance allocation succeeded after shutdown\n");
        return 11;
    }
    UnmapViewOfFile(guestView);
    UnmapViewOfFile(hostView);
    CloseHandle(sharedMapping);
    std::puts("kernel_memory_host_tests: PASS (guest ranges, shared RAMIN allocator, and bounded unified-memory DMA resolver)");
    return 0;
}
