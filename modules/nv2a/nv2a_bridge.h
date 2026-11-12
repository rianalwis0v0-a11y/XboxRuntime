#pragma once

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standard XboxRuntime module lifecycle.
__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);

// NV2A register offsets are relative to the 0xFD000000 MMIO aperture.
// Access sizes accepted by the upstream model are 1, 2, or 4 bytes.
__declspec(dllexport) uint64_t __cdecl XboxNV2A_ReadRegister(
    uint32_t register_offset,
    unsigned int access_size
);

__declspec(dllexport) int __cdecl XboxNV2A_WriteRegister(
    uint32_t register_offset,
    uint64_t value,
    unsigned int access_size
);

// Submit one Kelvin/PGRAPH method to the upstream NV2A→D3D8→D3D11 path.
__declspec(dllexport) int __cdecl XboxNV2A_SubmitMethod(
    uint32_t subchannel,
    uint32_t method,
    uint32_t parameter
);

__declspec(dllexport) void __cdecl XboxNV2A_Flush(void);

/* Bounded access to Xbox GPU-local memory. Offsets are byte offsets. */
__declspec(dllexport) int __cdecl XboxNV2A_ReadVRAM(uint32_t offset, void* destination, uint32_t length);
__declspec(dllexport) int __cdecl XboxNV2A_WriteVRAM(uint32_t offset, const void* source, uint32_t length);
__declspec(dllexport) int __cdecl XboxNV2A_ReadRAMIN(uint32_t offset, void* destination, uint32_t length);
__declspec(dllexport) int __cdecl XboxNV2A_WriteRAMIN(uint32_t offset, const void* source, uint32_t length);

/* Parse NV4-style incrementing/non-incrementing commands from VRAM.
 * Unsupported control-flow/long commands are rejected without partial dispatch. */
__declspec(dllexport) int __cdecl XboxNV2A_SubmitPushBuffer(uint32_t vram_offset, uint32_t byte_length, uint32_t* methods_submitted);

/* Interleaved method/parameter dword pairs. */
__declspec(dllexport) int __cdecl XboxNV2A_SubmitMethods(uint32_t subchannel, const uint32_t* pairs, uint32_t pair_count);


typedef struct XboxNV2A_STATS {
    uint32_t frames;
    uint32_t draw_calls;
    uint32_t vertices_submitted;
    uint32_t methods_handled;
    uint32_t methods_ignored;
    uint32_t clears;
} XboxNV2A_STATS;

__declspec(dllexport) int __cdecl XboxNV2A_GetStats(
    XboxNV2A_STATS* output
);

#ifdef __cplusplus
}
#endif
