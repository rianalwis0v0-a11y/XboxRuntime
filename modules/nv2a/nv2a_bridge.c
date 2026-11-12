#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "nv2a_bridge.h"
#include "nv2a_state.h"
#include "nv2a_pgraph_d3d11.h"
#include "d3d8_xbox.h"

#define XBOX_VRAM_SIZE  (64u * 1024u * 1024u)
#define XBOX_RAMIN_SIZE (1u * 1024u * 1024u)
#define XBOX_GPU_MEMORY_SIZE (XBOX_VRAM_SIZE + XBOX_RAMIN_SIZE)

static uint8_t* g_vram;
static uint8_t* g_ramin;

static uint8_t* g_gpu_memory;
static NV2AState* g_gpu;
static uint32_t g_bound_engine[32][8];
static uint32_t g_ref_counter[32];
static int g_started;
static uint32_t g_pgraph_fallback_methods;
static uint32_t g_pgraph_custom_methods;
typedef struct NV2A_SURFACE_REGS {
    uint32_t format, pitch, color_offset, zeta_offset;
    uint32_t clip_horizontal, clip_vertical;
    int dirty;
} NV2A_SURFACE_REGS;

static NV2A_SURFACE_REGS g_surface_regs;
static XboxNV2A_SURFACE_INFO g_bound_surface_info;
static IDirect3DSurface8* g_color_surface;
static IDirect3DSurface8* g_zeta_surface;
static int g_surface_bound;

static int update_surface_target(void);
static void sync_surface_to_vram(void);
static void release_surface_targets(void);


static int map_compare_function(uint32_t parameter, uint32_t* value)
{
    if (parameter < 0x0200u || parameter > 0x0207u) return 0;
    *value = parameter - 0x01FFu;
    return 1;
}

static int map_stencil_operation(uint32_t parameter, uint32_t* value)
{
    switch (parameter) {
    case 0x1E00u: *value = 1u; return 1;
    case 0x0000u: *value = 2u; return 1;
    case 0x1E01u: *value = 3u; return 1;
    case 0x1E02u: *value = 4u; return 1;
    case 0x1E03u: *value = 5u; return 1;
    case 0x150Au: *value = 6u; return 1;
    case 0x8507u: *value = 7u; return 1;
    case 0x8508u: *value = 8u; return 1;
    default: return 0;
    }
}

int __cdecl XboxNV2A_MapRenderState(uint32_t method, uint32_t parameter,
                                    uint32_t* state_out, uint32_t* value_out)
{
    uint32_t state = 0, value = parameter;
    if (!state_out || !value_out) return 0;

    switch (method) {
    case NV097_SET_ALPHA_FUNC:
        state = D3DRS_ALPHAFUNC;
        if (!map_compare_function(parameter, &value)) return 0;
        break;
    case NV097_SET_ALPHA_REF:
        state = D3DRS_ALPHAREF; value = parameter & 0xFFu; break;
    case NV097_SET_DEPTH_FUNC:
        state = D3DRS_ZFUNC;
        if (!map_compare_function(parameter, &value)) return 0;
        break;
    case NV097_SET_DEPTH_MASK:
        state = D3DRS_ZWRITEENABLE; value = parameter ? 1u : 0u; break;
    case NV097_SET_STENCIL_TEST_ENABLE:
        state = D3DRS_STENCILENABLE; value = parameter ? 1u : 0u; break;
    case NV097_SET_STENCIL_MASK:
        state = D3DRS_STENCILWRITEMASK; break;
    case NV097_SET_STENCIL_FUNC:
        state = D3DRS_STENCILFUNC;
        if (!map_compare_function(parameter, &value)) return 0;
        break;
    case NV097_SET_STENCIL_FUNC_REF:
        state = D3DRS_STENCILREF; break;
    case NV097_SET_STENCIL_FUNC_MASK:
        state = D3DRS_STENCILMASK; break;
    case NV097_SET_STENCIL_OP_FAIL:
        state = D3DRS_STENCILFAIL;
        if (!map_stencil_operation(parameter, &value)) return 0;
        break;
    case NV097_SET_STENCIL_OP_ZFAIL:
        state = D3DRS_STENCILZFAIL;
        if (!map_stencil_operation(parameter, &value)) return 0;
        break;
    case NV097_SET_STENCIL_OP_ZPASS:
        state = D3DRS_STENCILPASS;
        if (!map_stencil_operation(parameter, &value)) return 0;
        break;
    case NV097_SET_BLEND_EQUATION:
        state = D3DRS_BLENDOP;
        switch (parameter) {
        case 0x8006u: value = 1u; break;
        case 0x800Au: value = 2u; break;
        case 0x800Bu: value = 3u; break;
        case 0x8007u: value = 4u; break;
        case 0x8008u: value = 5u; break;
        default: return 0;
        }
        break;
    case NV097_SET_FOG_ENABLE:
        state = D3DRS_FOGENABLE; value = parameter ? 1u : 0u; break;
    case NV097_SET_FOG_COLOR:
        state = D3DRS_FOGCOLOR; break;
    case NV097_SET_FOG_MODE:
        state = D3DRS_FOGTABLEMODE;
        switch (parameter) {
        case 0x2601u: case 0x0804u: value = D3DFOG_LINEAR; break;
        case 0x0800u: case 0x0802u: value = D3DFOG_EXP; break;
        case 0x0801u: case 0x0803u: value = D3DFOG_EXP2; break;
        default: return 0;
        }
        break;
    case NV097_SET_LIGHTING_ENABLE:
        state = D3DRS_LIGHTING; value = parameter ? 1u : 0u; break;
    case NV097_SET_SHADE_MODE:
        state = D3DRS_SHADEMODE;
        if (parameter == NV097_SET_SHADE_MODE_V_FLAT) value = 1u;
        else if (parameter == NV097_SET_SHADE_MODE_V_SMOOTH) value = 2u;
        else return 0;
        break;
    case NV097_SET_CULL_FACE:
        state = D3DRS_CULLMODE;
        if (parameter == NV097_SET_CULL_FACE_V_FRONT) value = D3DCULL_CW;
        else if (parameter == NV097_SET_CULL_FACE_V_BACK) value = D3DCULL_CCW;
        else return 0; /* FRONT_AND_BACK has no equivalent D3D8 cull mode. */
        break;
    case NV097_SET_POINT_SIZE:
        state = D3DRS_POINTSIZE; break;
    default:
        return 0;
    }

    *state_out = state;
    *value_out = value;
    return 1;
}

static int apply_nv2a_render_state(uint32_t method, uint32_t parameter)
{
    uint32_t state = 0, value = 0;
    if (!XboxNV2A_MapRenderState(method, parameter, &state, &value))
        return 0;

    IDirect3DDevice8* device = xbox_GetD3DDevice();
    if (!device || !device->lpVtbl || !device->lpVtbl->SetRenderState)
        return 0;

    return SUCCEEDED(device->lpVtbl->SetRenderState(
        device, (D3DRENDERSTATETYPE)state, value)) ? 1 : 0;
}

static void submit_pgraph_method(uint32_t subchannel, uint32_t method,
                                 uint32_t parameter)
{
    switch (method) {
    case NV097_SET_SURFACE_FORMAT: g_surface_regs.format = parameter; g_surface_regs.dirty = 1; break;
    case NV097_SET_SURFACE_PITCH: g_surface_regs.pitch = parameter; g_surface_regs.dirty = 1; break;
    case NV097_SET_SURFACE_COLOR_OFFSET: g_surface_regs.color_offset = parameter; g_surface_regs.dirty = 1; break;
    case NV097_SET_SURFACE_ZETA_OFFSET: g_surface_regs.zeta_offset = parameter; g_surface_regs.dirty = 1; break;
    case NV097_SET_SURFACE_CLIP_HORIZONTAL: g_surface_regs.clip_horizontal = parameter; g_surface_regs.dirty = 1; break;
    case NV097_SET_SURFACE_CLIP_VERTICAL: g_surface_regs.clip_vertical = parameter; g_surface_regs.dirty = 1; break;
    default: break;
    }

    if ((method == NV097_SET_BEGIN_END && parameter != 0u) ||
        method == NV097_CLEAR_SURFACE)
        (void)update_surface_target();
    /* These real NV097 offsets collide with incorrect aliases in the
     * pinned translator. Apply them here and do not feed them to that switch. */
    if (method == NV097_SET_DEPTH_FUNC ||
        method == NV097_SET_CULL_FACE ||
        method == NV097_SET_SHADE_MODE ||
        method == NV097_SET_STENCIL_FUNC_REF) {
        if (apply_nv2a_render_state(method, parameter))
            ++g_pgraph_custom_methods;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }

    uint32_t dispatch_method = method;
    switch (method) {
    case NV097_SET_COLOR_CLEAR_VALUE:       dispatch_method = 0x01D4u; break;
    case NV097_CLEAR_SURFACE:               dispatch_method = 0x01D0u; break;
    case NV097_SET_CLEAR_RECT_HORIZONTAL:   dispatch_method = 0x01D8u; break;
    case NV097_SET_CLEAR_RECT_VERTICAL:     dispatch_method = 0x01DCu; break;
    case NV097_SET_DEPTH_TEST_ENABLE:       dispatch_method = 0x0354u; break;
    case NV097_SET_CULL_FACE_ENABLE:        dispatch_method = 0x039Cu; break;
    default: break;
    }

    const int fallback_applied = apply_nv2a_render_state(method, parameter);
    if (fallback_applied && dispatch_method == method)
        ++g_pgraph_fallback_methods;

    pgraph_method(g_gpu, subchannel, dispatch_method, parameter);
    if (dispatch_method != method)
        g_gpu->pgraph.regs[method / 4u] = parameter;
}


int __cdecl ModuleStart(void)
{
    if (g_started)
        return 0;

    // NV2A state is a process-wide singleton without a destroy API.
    // Reuse its original backing allocation after ModuleStop.
    if (!g_gpu) {
        g_gpu_memory = (uint8_t*)VirtualAlloc(NULL, XBOX_GPU_MEMORY_SIZE,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!g_gpu_memory) {
            OutputDebugStringW(L"[XboxRuntime:NV2A] VRAM/RAMIN allocation failed.\n");
            return 1;
        }
        g_vram = g_gpu_memory;
        g_ramin = g_gpu_memory + XBOX_VRAM_SIZE;
        g_gpu = nv2a_init_standalone(g_vram, XBOX_VRAM_SIZE, g_ramin, XBOX_RAMIN_SIZE);
        if (!g_gpu) {
            VirtualFree(g_gpu_memory, 0, MEM_RELEASE);
            g_gpu_memory = g_vram = g_ramin = NULL;
            OutputDebugStringW(L"[XboxRuntime:NV2A] nv2a_init_standalone failed.\n");
            return 2;
        }
    }

    /* PFIFO channel bindings belong to the bridge lifecycle. */
    memset(g_bound_engine, 0, sizeof(g_bound_engine));
    memset(g_ref_counter, 0, sizeof(g_ref_counter));
    g_pgraph_fallback_methods = 0;
    g_pgraph_custom_methods = 0;

    // Initializes the PGRAPH method translator. Its D3D8 device is supplied
    // by xbox_d3d8 when the game/runtime creates the Xbox D3D device.
    pgraph_d3d11_init();

    g_started = 1;
    OutputDebugStringW(L"[XboxRuntime:NV2A] NV2A + PGRAPH translator ready.\n");
    return 0;
}

void __cdecl ModuleStop(void)
{
    if (!g_started)
        return;

    pgraph_d3d11_flush();
    pgraph_d3d11_shutdown();

    // The upstream NV2A singleton currently has no public destroy/reset API.
    // Keep its backing allocation alive until the host process exits; freeing
    // it here would leave the singleton's VRAM/RAMIN pointers dangling.
    g_started = 0;
    OutputDebugStringW(L"[XboxRuntime:NV2A] Translator stopped. GPU memory retained until process exit.\n");
}

uint64_t __cdecl XboxNV2A_ReadRegister(
    uint32_t register_offset,
    unsigned int access_size
)
{
    if (!g_started || !g_gpu)
        return 0;

    if (access_size != 1 && access_size != 2 && access_size != 4)
        return 0;

    return nv2a_mmio_read(g_gpu, (hwaddr)register_offset, access_size);
}

int __cdecl XboxNV2A_WriteRegister(
    uint32_t register_offset,
    uint64_t value,
    unsigned int access_size
)
{
    if (!g_started || !g_gpu)
        return 1;

    if (access_size != 1 && access_size != 2 && access_size != 4)
        return 2;

    nv2a_mmio_write(g_gpu, (hwaddr)register_offset, value, access_size);
    return 0;
}

int __cdecl XboxNV2A_SubmitMethod(
    uint32_t subchannel,
    uint32_t method,
    uint32_t parameter
)
{
    if (!g_started || !g_gpu)
        return 1;

    if (subchannel > 7 || method >= 0x2000u * 4u || (method & 3u))
        return 2;

    submit_pgraph_method(subchannel, method, parameter);
    return 0;
}


static int memory_range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return offset <= capacity && length <= capacity - offset;
}

int __cdecl XboxNV2A_DecodeSurface(uint32_t surface_format,
    uint32_t surface_pitch, uint32_t color_offset, uint32_t zeta_offset,
    uint32_t clip_horizontal, uint32_t clip_vertical,
    XboxNV2A_SURFACE_INFO* output)
{
    if (!output) return 1;
    memset(output, 0, sizeof(*output));

    const uint32_t type = (surface_format >> 8) & 0xFu;
    const uint32_t aa = (surface_format >> 12) & 0xFu;
    const uint32_t color = surface_format & 0xFu;
    const uint32_t zeta = (surface_format >> 4) & 0xFu;
    const uint32_t width = (clip_horizontal >> 16) & 0xFFFFu;
    const uint32_t height = (clip_vertical >> 16) & 0xFFFFu;
    const uint32_t color_pitch = surface_pitch & 0xFFFFu;
    const uint32_t zeta_pitch = surface_pitch >> 16;

    /* The first backend only supports linear/pitch surfaces without MSAA. */
    if (type != 1u) return 2;
    if (aa != 0u) return 3;
    if (!width || !height || width > 16384u || height > 16384u)
        return 4;

    uint32_t d3d_color = 0, color_bpp = 0;
    switch (color) {
    case 0x01u: d3d_color = D3DFMT_X1R5G5B5; color_bpp = 2u; break;
    case 0x02u: d3d_color = D3DFMT_A1R5G5B5; color_bpp = 2u; break;
    case 0x03u: d3d_color = D3DFMT_R5G6B5; color_bpp = 2u; break;
    case 0x04u: case 0x05u:
        d3d_color = D3DFMT_X8R8G8B8; color_bpp = 4u; break;
    case 0x06u: case 0x07u: case 0x08u:
        d3d_color = D3DFMT_A8R8G8B8; color_bpp = 4u; break;
    default: return 5;
    }

    const uint64_t color_row = (uint64_t)width * color_bpp;
    const uint64_t color_span = (uint64_t)color_pitch * (height - 1u) + color_row;
    if (color_offset > XBOX_VRAM_SIZE || !color_pitch || color_pitch < color_row ||
        color_span > (uint64_t)XBOX_VRAM_SIZE - color_offset)
        return 6;

    uint32_t d3d_zeta = 0, zeta_bpp = 0;
    if (zeta == 0u) {
        if (zeta_pitch != 0u || zeta_offset != 0u) return 7;
    } else if (zeta == 1u) {
        d3d_zeta = D3DFMT_D16; zeta_bpp = 2u;
    } else if (zeta == 2u) {
        d3d_zeta = D3DFMT_D24S8; zeta_bpp = 4u;
    } else {
        return 7;
    }

    if (zeta_bpp) {
        const uint64_t zeta_row = (uint64_t)width * zeta_bpp;
        const uint64_t zeta_span = (uint64_t)zeta_pitch * (height - 1u) + zeta_row;
        if (!zeta_pitch || zeta_pitch < zeta_row ||
            zeta_offset > XBOX_VRAM_SIZE ||
            zeta_span > (uint64_t)XBOX_VRAM_SIZE - zeta_offset)
            return 8;
    }

    output->width = width;
    output->height = height;
    output->color_format = d3d_color;
    output->zeta_format = d3d_zeta;
    output->color_bytes_per_pixel = color_bpp;
    output->zeta_bytes_per_pixel = zeta_bpp;
    output->color_pitch = color_pitch;
    output->zeta_pitch = zeta_pitch;
    output->color_offset = color_offset;
    output->zeta_offset = zeta_offset;
    return 0;
}

int __cdecl XboxNV2A_ReadVRAM(uint32_t offset, void* destination, uint32_t length)
{
    if (!g_started || !destination || !memory_range_valid(offset, length, XBOX_VRAM_SIZE)) return 1;
    if (length) memcpy(destination, g_vram + offset, length);
    return 0;
}

int __cdecl XboxNV2A_WriteVRAM(uint32_t offset, const void* source, uint32_t length)
{
    if (!g_started || (!source && length) || !memory_range_valid(offset, length, XBOX_VRAM_SIZE)) return 1;
    if (length) memcpy(g_vram + offset, source, length);
    return 0;
}

int __cdecl XboxNV2A_ReadRAMIN(uint32_t offset, void* destination, uint32_t length)
{
    if (!g_started || !destination || !memory_range_valid(offset, length, XBOX_RAMIN_SIZE)) return 1;
    if (length) memcpy(destination, g_ramin + offset, length);
    return 0;
}

int __cdecl XboxNV2A_WriteRAMIN(uint32_t offset, const void* source, uint32_t length)
{
    if (!g_started || (!source && length) || !memory_range_valid(offset, length, XBOX_RAMIN_SIZE)) return 1;
    if (length) memcpy(g_ramin + offset, source, length);
    return 0;
}

static uint32_t read_le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ramht_lookup(uint32_t channel, uint32_t handle,
                        uint32_t* context_out)
{
    if (!g_started || !g_gpu || !context_out || channel > 31u)
        return 1;

    const uint32_t config = (uint32_t)nv2a_mmio_read(g_gpu, 0x2210u, 4);
    const uint32_t size_code = (config >> 16) & 3u;
    const uint32_t search_code = (config >> 24) & 3u;
    const uint32_t table_bytes = 0x1000u << size_code;
    const uint32_t entries = table_bytes / 8u;
    const uint32_t hash_bits = 9u + size_code;
    const uint32_t search_count = 16u << (search_code * 1u);
    const uint32_t base = (config & 0x1F0u) << 8;

    if (!memory_range_valid(base, table_bytes, XBOX_RAMIN_SIZE))
        return 2;

    uint32_t hash = 0;
    uint32_t value = handle;
    const uint32_t chunk_mask = (1u << hash_bits) - 1u;
    while (value) {
        hash ^= value & chunk_mask;
        value >>= hash_bits;
    }
    hash ^= channel << (hash_bits - 4u);
    hash &= entries - 1u;

    const uint32_t probes = search_count < entries ? search_count : entries;
    for (uint32_t probe = 0; probe < probes; ++probe) {
        const uint32_t index = (hash + probe) & (entries - 1u);
        const uint8_t* entry = g_ramin + base + index * 8u;
        const uint32_t stored_handle = read_le32(entry);
        const uint32_t context = read_le32(entry + 4u);
        if (context & 0x80000000u) {
            const uint32_t stored_channel = (context >> 24) & 0x1Fu;
            if (stored_channel == channel && stored_handle == handle) {
                *context_out = context;
                return 0;
            }
        }
    }
    return 3;
}

int __cdecl XboxNV2A_LookupRAMHT(uint32_t channel, uint32_t handle,
                                 uint32_t* context_out)
{
    if (context_out) *context_out = 0;
    return ramht_lookup(channel, handle, context_out);
}

int __cdecl XboxNV2A_ResolveDMAHandle(uint32_t channel, uint32_t handle,
                                      XboxNV2A_DMA_INFO* output)
{
    uint32_t context = 0;
    if (!output) return 1;
    memset(output, 0, sizeof(*output));

    const int lookup = ramht_lookup(channel, handle, &context);
    if (lookup != 0) return lookup;

    const uint32_t instance = (context & 0xFFFFu) << 4;
    if (!memory_range_valid(instance, 12u, XBOX_RAMIN_SIZE))
        return 4;

    const uint8_t* object = g_ramin + instance;
    const uint32_t flags = read_le32(object);
    const uint32_t limit = read_le32(object + 4u);
    const uint32_t frame = read_le32(object + 8u);
    const uint32_t dma_class = flags & 0xFFFu;
    const uint32_t target = flags & 0x00030000u;
    const uint32_t address = (frame & 0xFFFFF000u) | (flags & 0xFFF00000u);
    const uint32_t base = address & 0x07FFFFFFu;

    if (dma_class != 0x02u && dma_class != 0x03u && dma_class != 0x3Du)
        return 5;
    /* This standalone backend currently maps only linear local VRAM. */
    if (target != 0u)
        return 6;
    /* Match the upstream DMA mapping contract: limit is the mapped span. */
    if (base >= XBOX_VRAM_SIZE || limit > XBOX_VRAM_SIZE - base)
        return 7;

    output->handle = handle;
    output->instance_offset = instance;
    output->dma_class = dma_class;
    output->dma_target = target;
    output->engine = (context & 0x00030000u) >> 16;
    output->base_address = base;
    output->limit = limit;
    output->mapped_length = limit;
    return 0;
}


static int dispatch_puller_method(uint32_t channel, uint32_t subchannel,
                                  uint32_t method, uint32_t parameter)
{
    if (method == 0x0000u) {
        uint32_t context = 0;
        if (ramht_lookup(channel, parameter, &context) != 0)
            return 1;
        const uint32_t engine = (context & 0x00030000u) >> 16;
        const uint32_t instance = (context & 0xFFFFu) << 4;
        if (engine != 1u) return 2; /* This bridge currently drives PGRAPH only. */
        if (!memory_range_valid(instance, 4u, XBOX_RAMIN_SIZE)) return 3;
        submit_pgraph_method(subchannel, 0u, instance);
        g_bound_engine[channel][subchannel] = engine;
        return 0;
    }

    if (method == 0x0050u) {
        g_ref_counter[channel] = parameter;
        return 0;
    }

    if (method < 0x0100u || g_bound_engine[channel][subchannel] != 1u)
        return 4;

    /* 0x180..0x1fc methods carry a RAMHT handle, not an immediate value. */
    if (method >= 0x0180u && method < 0x0200u) {
        uint32_t context = 0;
        if (ramht_lookup(channel, parameter, &context) != 0)
            return 5;
        parameter = (context & 0xFFFFu) << 4;
        if (!memory_range_valid(parameter, 4u, XBOX_RAMIN_SIZE))
            return 6;
    }

    submit_pgraph_method(subchannel, method, parameter);
    return 0;
}

static int validate_puller_method(uint32_t channel, uint32_t subchannel,
                                      uint32_t method, uint32_t parameter,
                                      uint32_t bound_engines[8])
{
    if (method == 0x0000u) {
        uint32_t context = 0;
        if (ramht_lookup(channel, parameter, &context) != 0) return 1;
        const uint32_t engine = (context & 0x00030000u) >> 16;
        const uint32_t instance = (context & 0xFFFFu) << 4;
        if (engine != 1u || !memory_range_valid(instance, 4u, XBOX_RAMIN_SIZE))
            return 2;
        bound_engines[subchannel] = engine;
        return 0;
    }
    if (method == 0x0050u) return 0;
    if (method < 0x0100u || bound_engines[subchannel] != 1u) return 3;
    if (method >= 0x0180u && method < 0x0200u) {
        uint32_t context = 0;
        if (ramht_lookup(channel, parameter, &context) != 0) return 4;
        const uint32_t instance = (context & 0xFFFFu) << 4;
        if (!memory_range_valid(instance, 4u, XBOX_RAMIN_SIZE)) return 5;
    }
    return 0;
}

static int walk_dma_pushbuffer(uint32_t channel, uint32_t base,
                               uint32_t mapped_length, uint32_t initial_get,
                               uint32_t put, uint32_t* final_get,
                               uint32_t* methods_submitted, int dispatch)
{
    uint32_t get = initial_get;
    uint32_t methods = 0;
    uint32_t steps = 0;
    uint32_t method = 0, subchannel = 0, remaining = 0;
    uint32_t validation_engines[8];
    memcpy(validation_engines, g_bound_engine[channel], sizeof(validation_engines));
    int non_incrementing = 0;
    int subroutine_active = 0;
    uint32_t subroutine_return = 0;

    if ((get & 3u) || (put & 3u) || get > put || put > mapped_length)
        return 1;

    while (get != put) {
        if (++steps > 1048576u || get > mapped_length || mapped_length - get < 4u)
            return 2;

        const uint32_t word = read_le32(g_vram + base + get);
        get += 4u;

        if (remaining) {
            if (++methods > 1048576u) return 3;
            if (dispatch) {
                const int result = dispatch_puller_method(channel, subchannel,
                                                          method, word);
                if (result != 0) return 10 + result;
            } else {
                const int result = validate_puller_method(channel, subchannel,
                                                          method, word,
                                                          validation_engines);
                if (result != 0) return 10 + result;
            }
            if (!non_incrementing) method += 4u;
            --remaining;
            continue;
        }

        if ((word & 0xE0000003u) == 0x20000000u) {
            /* Legacy 29-bit jump command. */
            const uint32_t target = word & 0x1FFFFFFCu;
            if ((target & 3u) || target >= mapped_length)
                return 4;
            get = target;
        } else if ((word & 3u) == 1u) {
            const uint32_t target = word & 0xFFFFFFFCu;
            if (target >= mapped_length)
                return 4;
            get = target;
        } else if ((word & 3u) == 2u) {
            const uint32_t target = word & 0xFFFFFFFCu;
            if (subroutine_active || target >= mapped_length)
                return 5;
            subroutine_return = get;
            subroutine_active = 1;
            get = target;
        } else if (word == 0x00020000u) {
            if (!subroutine_active) return 6;
            get = subroutine_return;
            subroutine_active = 0;
        } else if ((word & 0xE0030003u) == 0u) {
            method = ((word >> 2) & 0x7FFu) * 4u;
            subchannel = (word >> 13) & 7u;
            remaining = (word >> 18) & 0x7FFu;
            non_incrementing = 0;
            if (!remaining || remaining > (mapped_length - get) / 4u ||
                remaining > 0x800u || method > 0x1FFCu ||
                remaining - 1u > (0x1FFCu - method) / 4u)
                return 7;
        } else if ((word & 0xE0030003u) == 0x40000000u) {
            method = ((word >> 2) & 0x7FFu) * 4u;
            subchannel = (word >> 13) & 7u;
            remaining = (word >> 18) & 0x7FFu;
            non_incrementing = 1;
            if (!remaining || remaining > (mapped_length - get) / 4u ||
                method > 0x1FFCu)
                return 7;
        } else {
            return 8; /* Includes IB-only and reserved command encodings. */
        }
    }

    if (remaining || subroutine_active) return 9;
    if (final_get) *final_get = get;
    if (methods_submitted) *methods_submitted = methods;
    return 0;
}

int __cdecl XboxNV2A_SubmitDMAPushBuffer(uint32_t channel, uint32_t handle,
                                         uint32_t get, uint32_t put,
                                         uint32_t* new_get,
                                         uint32_t* methods_submitted)
{
    XboxNV2A_DMA_INFO dma;
    if (new_get) *new_get = get;
    if (methods_submitted) *methods_submitted = 0;
    if (!g_started || !g_gpu || channel > 31u) return 1;

    int result = XboxNV2A_ResolveDMAHandle(channel, handle, &dma);
    if (result != 0) return 2 + result;
    if (dma.dma_class != 0x02u && dma.dma_class != 0x3Du)
        return 8; /* Pushbuffer must be readable. */
    if (get > put || put > dma.mapped_length || (get & 3u) || (put & 3u))
        return 9;

    uint32_t checked_get = get, checked_methods = 0;
    result = walk_dma_pushbuffer(channel, dma.base_address, dma.mapped_length,
                                 get, put, &checked_get, &checked_methods, 0);
    if (result != 0) return 16 + result;

    /* Only dispatch after the entire stream, including control flow, validates. */
    uint32_t executed_get = get, executed_methods = 0;
    result = walk_dma_pushbuffer(channel, dma.base_address, dma.mapped_length,
                                 get, put, &executed_get, &executed_methods, 1);
    if (result != 0) return 32 + result;
    if (new_get) *new_get = executed_get;
    if (methods_submitted) *methods_submitted = executed_methods;
    return 0;
}

static int walk_pushbuffer(uint32_t offset, uint32_t byte_length, uint32_t* submitted, int dispatch)
{
    if ((offset & 3u) || (byte_length & 3u) ||
        !memory_range_valid(offset, byte_length, XBOX_VRAM_SIZE))
        return 1;

    uint32_t words = byte_length / 4u;
    uint32_t cursor = 0;
    uint32_t total = 0;
    while (cursor < words) {
        uint32_t header;
        memcpy(&header, g_vram + offset + cursor * 4u, sizeof(header));
        ++cursor;

        /* NV4-style method packets: 000 = incrementing, 010 = non-incrementing.
         * The packet carries an 11-bit word count, subchannel, and byte method. */
        uint32_t command_type = header >> 29;
        if (command_type != 0u && command_type != 2u)
            return 2; /* Reject jump/call/return/long/unknown commands. */

        uint32_t count = (header >> 18) & 0x7FFu;
        uint32_t subchannel = (header >> 13) & 7u;
        uint32_t method = header & 0x1FFCu;
        if (!count || method >= 0x2000u * 4u ||
            count > words - cursor || count > 1048576u - total ||
            (command_type == 0u && count - 1u > (0x1FFCu - method) / 4u))
            return 3;

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t parameter;
            memcpy(&parameter, g_vram + offset + (cursor + i) * 4u, sizeof(parameter));
            if (dispatch)
                submit_pgraph_method(subchannel,
                    command_type == 2u ? method : method + i * 4u,
                    parameter);
        }
        cursor += count;
        total += count;
    }

    if (submitted) *submitted = total;
    return 0;
}

int __cdecl XboxNV2A_SubmitPushBuffer(uint32_t vram_offset, uint32_t byte_length, uint32_t* methods_submitted)
{
    if (methods_submitted) *methods_submitted = 0;
    if (!g_started || !g_gpu) return 4;

    /* Validate the complete stream before causing any GPU state changes. */
    int result = walk_pushbuffer(vram_offset, byte_length, NULL, 0);
    if (result != 0) return result;
    return walk_pushbuffer(vram_offset, byte_length, methods_submitted, 1);
}

int __cdecl XboxNV2A_SubmitMethods(uint32_t subchannel, const uint32_t* pairs, uint32_t pair_count)
{
    if (!g_started || !g_gpu || subchannel > 7 || (!pairs && pair_count)) return 1;
    if (pair_count > 1048576u) return 2;
    for (uint32_t i = 0; i < pair_count; ++i) {
        uint32_t method = pairs[i * 2u];
        if (method >= 0x2000u * 4u || (method & 3u)) return 3;
    }
    for (uint32_t i = 0; i < pair_count; ++i)
        submit_pgraph_method(subchannel, pairs[i * 2u], pairs[i * 2u + 1u]);
    return 0;
}

void __cdecl XboxNV2A_Flush(void)
{
    if (g_started)
        pgraph_d3d11_flush();
}

int __cdecl XboxNV2A_GetStats(XboxNV2A_STATS* output)
{
    PgraphD3D11Stats stats;

    if (!g_started || !output)
        return 1;

    pgraph_d3d11_get_stats(&stats);
    output->frames = stats.frames;
    output->draw_calls = stats.draw_calls;
    output->vertices_submitted = stats.vertices_submitted;
    output->methods_handled = stats.methods_handled + g_pgraph_custom_methods;
    output->methods_ignored = stats.methods_ignored >= g_pgraph_fallback_methods
        ? stats.methods_ignored - g_pgraph_fallback_methods : 0;
    output->clears = stats.clears;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
