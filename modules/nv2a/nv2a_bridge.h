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


/* Map selected NV097 fixed-function methods to D3D8 render-state IDs/values.
 * Returns 1 for a supported mapping, 0 for unsupported/invalid input. */
__declspec(dllexport) int __cdecl XboxNV2A_MapRenderState(
    uint32_t method, uint32_t parameter,
    uint32_t* d3d_state_out, uint32_t* d3d_value_out);

typedef struct XboxNV2A_SURFACE_INFO {
    uint32_t width;
    uint32_t height;
    uint32_t color_format;
    uint32_t zeta_format;
    uint32_t color_bytes_per_pixel;
    uint32_t zeta_bytes_per_pixel;
    uint32_t color_pitch;
    uint32_t zeta_pitch;
    uint32_t color_offset;
    uint32_t zeta_offset;
} XboxNV2A_SURFACE_INFO;

/* Decode supported NV097 texture formats to tightly packed RGBA8.
 * format is the NV097_SET_TEXTURE_FORMAT method value. source_pitch=0 uses
 * the minimum row pitch. Compressed DXT1 input uses 8-byte 4x4 blocks. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeTextureRGBA8(
    uint32_t format, uint32_t width, uint32_t height, uint32_t source_pitch,
    const void* source, uint32_t source_length,
    void* destination, uint32_t destination_capacity,
    uint32_t* destination_pitch);

/* Decode a power-of-two Xbox Morton-swizzled texture into RGBA8. DXT is not accepted here. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeSwizzledTextureRGBA8(
    uint32_t format, uint32_t width, uint32_t height,
    const void* source, uint32_t source_length,
    void* destination, uint32_t destination_capacity,
    uint32_t* destination_pitch);

/* Decode a bounded NV097 mip chain into consecutive RGBA8 levels.
 * level_offsets and level_sizes each contain level_count uint32_t entries. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeTextureMipChainRGBA8(
    uint32_t format, uint32_t width, uint32_t height, uint32_t level_count,
    const void* source, uint32_t source_length,
    void* destination, uint32_t destination_capacity,
    uint32_t* level_offsets, uint32_t* level_sizes);

/* Decode a mip chain directly from a bound local-VRAM texture DMA context.
 * context_method selects one of the four NV097 context-DMA slots (0x180..0x18c,
 * four-byte aligned), corresponding to DMA_A through DMA_D. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeTextureMipChainFromDMA(
    uint32_t channel, uint32_t subchannel, uint32_t context_method,
    uint32_t texture_offset, uint32_t format, uint32_t width,
    uint32_t height, uint32_t level_count, void* destination,
    uint32_t destination_capacity, uint32_t* level_offsets,
    uint32_t* level_sizes);
/* Decode one pitched texture level from any bound local-VRAM DMA context. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeTextureFromDMA(
    uint32_t channel, uint32_t subchannel, uint32_t context_method,
    uint32_t texture_offset, uint32_t format, uint32_t width,
    uint32_t height, uint32_t source_pitch, void* destination,
    uint32_t destination_capacity, uint32_t* destination_pitch);

/* Map NV097 texture-filter fields to D3D8 minification, magnification and mip filters. */
__declspec(dllexport) int __cdecl XboxNV2A_MapTextureFilter(
    uint32_t parameter, uint32_t* min_filter_out,
    uint32_t* mag_filter_out, uint32_t* mip_filter_out);

/* Decode a linear-pitch NV097 surface and validate its VRAM spans. */
__declspec(dllexport) int __cdecl XboxNV2A_DecodeSurface(
    uint32_t surface_format, uint32_t surface_pitch,
    uint32_t color_offset, uint32_t zeta_offset,
    uint32_t clip_horizontal, uint32_t clip_vertical,
    XboxNV2A_SURFACE_INFO* output);

typedef struct XboxNV2A_DMA_INFO {
    uint32_t handle;
    uint32_t instance_offset;
    uint32_t dma_class;
    uint32_t dma_target;
    uint32_t engine;
    uint32_t base_address;
    uint32_t limit;
    uint32_t mapped_length;
} XboxNV2A_DMA_INFO;

/* NV2A RAMHT lookup and validated local-VRAM DMA object resolution.
 * channel is the PFIFO channel ID (0..31); returned instance is a RAMIN byte offset. */
__declspec(dllexport) int __cdecl XboxNV2A_LookupRAMHT(
    uint32_t channel, uint32_t handle, uint32_t* context_out);
__declspec(dllexport) int __cdecl XboxNV2A_ResolveDMAHandle(
    uint32_t channel, uint32_t handle, XboxNV2A_DMA_INFO* output);
/* Execute an NV4-style DMA pushbuffer through a validated RAMHT DMA handle.
 * GET/PUT are byte offsets within the DMA object; nested calls are limited to one. */
__declspec(dllexport) int __cdecl XboxNV2A_SubmitDMAPushBuffer(
    uint32_t channel, uint32_t handle, uint32_t get, uint32_t put,
    uint32_t* new_get, uint32_t* methods_submitted);
typedef struct XboxNV2A_VERTEX {
    float x, y, z;
    float nx, ny, nz;
    uint32_t diffuse;
    uint32_t specular;
    float u, v;
    float u1, v1;
} XboxNV2A_VERTEX;

/* Build a host FVF stream from position, diffuse-color and TEX0 guest arrays. */
__declspec(dllexport) int __cdecl XboxNV2A_BuildVertexStream(
    uint32_t channel, uint32_t subchannel, uint32_t first_vertex,
    uint32_t vertex_count, XboxNV2A_VERTEX* destination,
    uint32_t destination_capacity, uint32_t* stride_out, uint32_t* fvf_out);
__declspec(dllexport) int __cdecl XboxNV2A_BuildIndexedVertexStream(
    uint32_t channel, uint32_t subchannel, const uint32_t* indices,
    uint32_t index_count, XboxNV2A_VERTEX* destination,
    uint32_t destination_capacity, uint32_t* stride_out,
    uint32_t* fvf_out);

/* Fetch packed vertex elements from the bound NV097 VERTEX_A/B DMA context. */
__declspec(dllexport) int __cdecl XboxNV2A_ReadVertexArray(
    uint32_t channel, uint32_t subchannel, uint32_t array_index,
    uint32_t first_vertex, uint32_t vertex_count,
    void* destination, uint32_t destination_capacity,
    uint32_t* stride_out, uint32_t* element_bytes_out);

/* Retrieve a resolved context-DMA object bound by NV097 methods 0x180..0x1fc. */
__declspec(dllexport) int __cdecl XboxNV2A_GetDMAContext(
    uint32_t channel, uint32_t subchannel, uint32_t context_method,
    XboxNV2A_DMA_INFO* output);

/* Submit using PFIFO CACHE1_DMA_INSTANCE (RAMIN byte offset, 16-byte aligned). */
__declspec(dllexport) int __cdecl XboxNV2A_SubmitDMAInstance(
    uint32_t channel, uint32_t instance_offset, uint32_t get, uint32_t put,
    uint32_t* new_get, uint32_t* methods_submitted);

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
