#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_bridge.h"
#include "nv2a_state.h"
#include "nv2a_pgraph_d3d11.h"
#include "d3d8_xbox.h"
#include "d3d8_swizzle.h"
#include "d3d8_vsh.h"

#define XBOX_VRAM_SIZE  (64u * 1024u * 1024u)
#define XBOX_RAMIN_SIZE (1u * 1024u * 1024u)
#define XBOX_GPU_MEMORY_SIZE (XBOX_VRAM_SIZE + XBOX_RAMIN_SIZE)
#define NV2A_MAX_BATCH_VERTICES 1024u

static uint8_t* g_vram;
static uint8_t* g_ramin;

static uint8_t* g_gpu_memory;
static NV2AState* g_gpu;
static uint32_t g_bound_engine[32][8];
static uint32_t g_ref_counter[32];
static XboxNV2A_DMA_INFO g_dma_contexts[32][8][32];
static int g_started;
static uint32_t g_transform_execution_mode;
/* Mirror NV2A program/constant upload ports into the existing D3D11 HLSL
 * translator. The translator compiles on the first draw and caches by
 * microcode hash; subsequent draws bind the cached ID3D11VertexShader. */
/* NV2A vertex register file mirrored from PFIFO/PGRAPH uploads.
 * The CPU owns register state only; vertex instructions execute on D3D11. */
static DWORD g_nv2a_vsh_program[136][4];
static float g_nv2a_vsh_constants[192][4];
static uint64_t g_nv2a_vsh_constant_dirty[3];
static uint32_t g_nv2a_vsh_load_slot, g_nv2a_vsh_load_word;
static uint32_t g_nv2a_vsh_start_slot, g_nv2a_vsh_constant_load;
static uint32_t g_nv2a_vsh_constant_word;
static uint32_t g_nv2a_vsh_context_write;
static DWORD g_nv2a_vsh_handle;
static uint16_t g_nv2a_vsh_input_mask;
static uint32_t g_nv2a_vsh_program_hash;
static uint8_t* g_nv2a_gpu_vertex_stream;
static size_t g_nv2a_gpu_vertex_capacity;
static int g_nv2a_vsh_dirty = 1;
static uint32_t g_pgraph_fallback_methods;
static uint32_t g_pgraph_custom_methods;
static uint32_t g_pgraph_custom_draws;
static uint32_t g_pgraph_custom_vertices;
static uint32_t g_primitive_mode[32][8];
static uint32_t g_element_indices[32][8][NV2A_MAX_BATCH_VERTICES];
static uint32_t g_element_count[32][8];
static int g_element_overflow[32][8];
typedef struct NV2A_TEXTURE_CACHE {
    IDirect3DTexture8* texture;
    IDirect3DDevice8* owner_device;
    uint32_t format, offset, control0, control1, image_rect;
    uint32_t dma_handle, dma_base, dma_length;
    uint32_t palette, palette_dma_handle, palette_dma_base, palette_dma_length;
    int dirty;
} NV2A_TEXTURE_CACHE;
static NV2A_TEXTURE_CACHE g_texture_cache[32][8][4];
static void mark_all_textures_dirty(void);
static void mark_context_textures_dirty(uint32_t channel, uint32_t subchannel);
static void release_texture_cache(void);


int __cdecl XboxNV2A_BuildIndexedVertexStream(uint32_t channel,
    uint32_t subchannel, const uint32_t* indices, uint32_t index_count,
    XboxNV2A_VERTEX* destination, uint32_t destination_capacity,
    uint32_t* stride_out, uint32_t* fvf_out)
{
    if (stride_out) *stride_out = 0;
    if (fvf_out) *fvf_out = 0;
    if (!indices || !destination || !index_count || index_count > NV2A_MAX_BATCH_VERTICES ||
        destination_capacity < index_count * (uint32_t)sizeof(XboxNV2A_VERTEX))
        return 1;

    uint32_t common_stride = 0, common_fvf = 0;
    for (uint32_t i = 0; i < index_count; ++i) {
        XboxNV2A_VERTEX vertex;
        uint32_t stride = 0, fvf = 0;
        const int result = XboxNV2A_BuildVertexStream(
            channel, subchannel, indices[i], 1u, &vertex,
            (uint32_t)sizeof(vertex), &stride, &fvf);
        if (result != 0) return 2 + result;
        if (stride != sizeof(XboxNV2A_VERTEX) ||
            (i && (stride != common_stride || fvf != common_fvf)))
            return 3;
        if (!i) {
            common_stride = stride;
            common_fvf = fvf;
        }
        destination[i] = vertex;
    }

    if (stride_out) *stride_out = common_stride;
    if (fvf_out) *fvf_out = common_fvf;
    return 0;
}

static int draw_nv2a_arrays(uint32_t channel, uint32_t subchannel, uint32_t parameter);
static int draw_nv2a_indexed(uint32_t channel,uint32_t subchannel,const uint32_t* indices,uint32_t count);
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

static int map_blend_factor(uint32_t parameter, uint32_t* value)
{
    if (!value) return 0;
    switch (parameter) {
    case 0x0000u: *value = D3DBLEND_ZERO; return 1;
    case 0x0001u: *value = D3DBLEND_ONE; return 1;
    case 0x0300u: *value = D3DBLEND_SRCCOLOR; return 1;
    case 0x0301u: *value = D3DBLEND_INVSRCCOLOR; return 1;
    case 0x0302u: *value = D3DBLEND_SRCALPHA; return 1;
    case 0x0303u: *value = D3DBLEND_INVSRCALPHA; return 1;
    case 0x0304u: *value = D3DBLEND_DESTALPHA; return 1;
    case 0x0305u: *value = D3DBLEND_INVDESTALPHA; return 1;
    case 0x0306u: *value = D3DBLEND_DESTCOLOR; return 1;
    case 0x0307u: *value = D3DBLEND_INVDESTCOLOR; return 1;
    case 0x0308u: *value = D3DBLEND_SRCALPHASAT; return 1;
    default: return 0;
    }
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
    case NV097_SET_ALPHA_TEST_ENABLE:
        state = D3DRS_ALPHATESTENABLE; value = parameter ? 1u : 0u; break;
    case NV097_SET_CULL_FACE_ENABLE:
        state = D3DRS_CULLMODE;
        value = parameter ? D3DCULL_CCW : D3DCULL_NONE;
        break;
    case NV097_SET_DEPTH_TEST_ENABLE:
        state = D3DRS_ZENABLE; value = parameter ? 1u : 0u; break;
    case 0x0304u:
        state = D3DRS_ALPHABLENDENABLE; value = parameter ? 1u : 0u; break;
    case 0x0344u:
        state = D3DRS_SRCBLEND;
        if (!map_blend_factor(parameter, &value)) return 0;
        break;
    case 0x0348u:
        state = D3DRS_DESTBLEND;
        if (!map_blend_factor(parameter, &value)) return 0;
        break;
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
        state = D3DRS_FOGCOLOR;
        /* NV097 packs R,G,B,A from least to most significant byte;
         * D3DCOLOR uses A,R,G,B. */
        value = ((parameter & 0x000000FFu) << 16) |
                (parameter & 0x0000FF00u) |
                ((parameter & 0x00FF0000u) >> 16) |
                (parameter & 0xFF000000u);
        break;
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

    /* CULL_FACE and CULL_FACE_ENABLE are independent NV097 registers.
     * Preserve the selected winding when toggling culling, and preserve
     * disabled culling when the face selector changes. */
    if (method == NV097_SET_CULL_FACE_ENABLE && parameter) {
        const uint32_t face = g_gpu->pgraph.regs[NV097_SET_CULL_FACE / 4u];
        if (face == NV097_SET_CULL_FACE_V_FRONT) value = D3DCULL_CW;
        else if (face == NV097_SET_CULL_FACE_V_BACK) value = D3DCULL_CCW;
    } else if (method == NV097_SET_CULL_FACE &&
               g_gpu->pgraph.regs[NV097_SET_CULL_FACE_ENABLE / 4u] == 0u) {
        value = D3DCULL_NONE;
    }

    IDirect3DDevice8* device = xbox_GetD3DDevice();
    if (!device || !device->lpVtbl || !device->lpVtbl->SetRenderState)
        return 0;

    return SUCCEEDED(device->lpVtbl->SetRenderState(
        device, (D3DRENDERSTATETYPE)state, value)) ? 1 : 0;
}


static int map_texture_address_mode(uint32_t mode, uint32_t* output)
{
    if (!output) return 0;
    switch(mode) {
    case 1u: *output=D3DTADDRESS_WRAP; return 1;
    case 2u: *output=D3DTADDRESS_MIRROR; return 1;
    case 3u: *output=D3DTADDRESS_CLAMP; return 1;
    case 4u: *output=D3DTADDRESS_BORDER; return 1;
    case 5u: *output=D3DTADDRESS_CLAMP; return 1;
    case 6u: *output=D3DTADDRESS_MIRRORONCE; return 1;
    default: return 0;
    }
}

int __cdecl XboxNV2A_MapTextureFilter(uint32_t parameter,
    uint32_t* min_filter_out, uint32_t* mag_filter_out,
    uint32_t* mip_filter_out)
{
    if (!min_filter_out || !mag_filter_out || !mip_filter_out)
        return 0;

    const uint32_t min_mode = (parameter & 0x00FF0000u) >> 16;
    const uint32_t mag_mode = (parameter & 0x0F000000u) >> 24;
    uint32_t min_filter = 0, mag_filter = 0, mip_filter = D3DTEXF_NONE;

    switch (min_mode) {
    case 1u: min_filter = D3DTEXF_POINT; break;
    case 2u: min_filter = D3DTEXF_LINEAR; break;
    case 3u: min_filter = D3DTEXF_POINT; mip_filter = D3DTEXF_POINT; break;
    case 4u: min_filter = D3DTEXF_LINEAR; mip_filter = D3DTEXF_POINT; break;
    case 5u: min_filter = D3DTEXF_POINT; mip_filter = D3DTEXF_LINEAR; break;
    case 6u: min_filter = D3DTEXF_LINEAR; mip_filter = D3DTEXF_LINEAR; break;
    case 7u: min_filter = D3DTEXF_LINEAR; break; /* Convolution fallback. */
    default: return 0;
    }

    switch (mag_mode) {
    case 1u: mag_filter = D3DTEXF_POINT; break;
    case 2u: case 4u: mag_filter = D3DTEXF_LINEAR; break;
    default: return 0;
    }

    *min_filter_out = min_filter;
    *mag_filter_out = mag_filter;
    *mip_filter_out = mip_filter;
    return 1;
}

static int apply_texture_stage_method(uint32_t method, uint32_t parameter)
{
    uint32_t stage=0, state_u=0, state_v=0;
    IDirect3DDevice8* device=xbox_GetD3DDevice();
    if(!device || !device->lpVtbl || !device->lpVtbl->SetTextureStageState)
        return 0;
    if(method>=0x1B08u && method<=0x1BC8u &&
       ((method-0x1B08u)%0x40u)==0u) {
        stage=(method-0x1B08u)/0x40u;
        if(stage>=4u ||
           !map_texture_address_mode(parameter&0xFu,&state_u) ||
           !map_texture_address_mode((parameter>>4)&0xFu,&state_v))
            return 0;
        if(FAILED(device->lpVtbl->SetTextureStageState(device,stage,
                D3DTSS_ADDRESSU,state_u))) return 0;
        if(FAILED(device->lpVtbl->SetTextureStageState(device,stage,
                D3DTSS_ADDRESSV,state_v))) return 0;
        return 1;
    }
    if (method >= 0x1B14u && method <= 0x1BD4u &&
        ((method - 0x1B14u) % 0x40u) == 0u) {
        uint32_t min_filter = 0, mag_filter = 0, mip_filter = 0;
        stage = (method - 0x1B14u) / 0x40u;
        if (stage >= 4u ||
            !XboxNV2A_MapTextureFilter(parameter, &min_filter,
                                       &mag_filter, &mip_filter))
            return 0;
        if (FAILED(device->lpVtbl->SetTextureStageState(
                device, stage, D3DTSS_MINFILTER, min_filter))) return 0;
        if (FAILED(device->lpVtbl->SetTextureStageState(
                device, stage, D3DTSS_MAGFILTER, mag_filter))) return 0;
        if (FAILED(device->lpVtbl->SetTextureStageState(
                device, stage, D3DTSS_MIPFILTER, mip_filter))) return 0;
        return 1;
    }
    if(method>=0x1B24u && method<=0x1BE4u &&
       ((method-0x1B24u)%0x40u)==0u) {
        stage=(method-0x1B24u)/0x40u;
        if(stage>=4u) return 0;
        return SUCCEEDED(device->lpVtbl->SetTextureStageState(device,stage,
            D3DTSS_BORDERCOLOR,parameter)) ? 1 : 0;
    }
    return 0;
}

static void submit_pgraph_method(uint32_t channel, uint32_t subchannel, uint32_t method,
                                 uint32_t parameter)
{
    /* Kelvin transform program and constant upload ports. */
    if (method == 0x1E94u) {
        g_transform_execution_mode = parameter;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method == 0x1E98u) {
        g_nv2a_vsh_context_write = parameter & 1u;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method == 0x1E9Cu) {
                g_nv2a_vsh_load_slot = parameter;
        g_nv2a_vsh_load_word = 0;
        g_nv2a_vsh_dirty = 1;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method == 0x1EA0u) {
                g_nv2a_vsh_start_slot = parameter;
        g_nv2a_vsh_dirty = 1;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method == 0x1EA4u) {
                g_nv2a_vsh_constant_load = parameter;
        g_nv2a_vsh_constant_word = 0;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method >= 0x0B00u && method < 0x0B80u) {
        if (g_nv2a_vsh_load_slot < 136u) {
            g_nv2a_vsh_program[g_nv2a_vsh_load_slot][g_nv2a_vsh_load_word] = parameter;
            if (++g_nv2a_vsh_load_word == 4u) {
                g_nv2a_vsh_load_word = 0;
                ++g_nv2a_vsh_load_slot;
            }
            g_nv2a_vsh_dirty = 1;
        }
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method >= 0x0B80u && method < 0x0C00u) {
        if (g_nv2a_vsh_constant_load < 192u) {
            memcpy(&g_nv2a_vsh_constants[g_nv2a_vsh_constant_load][g_nv2a_vsh_constant_word],
                   &parameter, sizeof(parameter));
            if (++g_nv2a_vsh_constant_word == 4u) {
                g_nv2a_vsh_constant_word = 0;
                g_nv2a_vsh_constant_dirty[g_nv2a_vsh_constant_load >> 6] |=
                    (uint64_t)1u << (g_nv2a_vsh_constant_load & 63u);
                ++g_nv2a_vsh_constant_load;
            }
        }
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }
    if (method >= 0x1B00u && method <= 0x1BDCu) {
        const uint32_t stage = (method - 0x1B00u) / 0x40u;
        const uint32_t reg = (method - 0x1B00u) % 0x40u;
        if (stage < 4u && (reg == 0x00u || reg == 0x04u ||
            reg == 0x0Cu || reg == 0x10u || reg == 0x1Cu || reg == 0x20u))
            g_texture_cache[channel][subchannel][stage].dirty = 1;
    }
    if (apply_texture_stage_method(method, parameter)) {
        ++g_pgraph_custom_methods;
        g_gpu->pgraph.regs[method / 4u] = parameter;
        return;
    }

    if (method == NV097_SET_BEGIN_END) {
        if (parameter == NV097_SET_BEGIN_END_OP_END) {
            if (g_element_count[channel][subchannel] && !g_element_overflow[channel][subchannel])
                (void)draw_nv2a_indexed(channel,subchannel,g_element_indices[channel][subchannel],g_element_count[channel][subchannel]);
            g_element_count[channel][subchannel]=0;g_element_overflow[channel][subchannel]=0;
            g_primitive_mode[channel][subchannel]=parameter;g_gpu->pgraph.regs[method/4u]=parameter;
            pgraph_method(g_gpu,subchannel,method,parameter);return;
        }
        g_primitive_mode[channel][subchannel]=parameter;g_element_count[channel][subchannel]=0;g_element_overflow[channel][subchannel]=0;
    }
    if(method==NV097_ARRAY_ELEMENT16||method==NV097_ARRAY_ELEMENT32){
        if(g_primitive_mode[channel][subchannel]==NV097_SET_BEGIN_END_OP_END)return;
        if(g_element_count[channel][subchannel]>=NV2A_MAX_BATCH_VERTICES){g_element_overflow[channel][subchannel]=1;return;}
        uint32_t index=method==NV097_ARRAY_ELEMENT16?(parameter&0xFFFFu):parameter;
        g_element_indices[channel][subchannel][g_element_count[channel][subchannel]++]=index;
        g_gpu->pgraph.regs[method/4u]=parameter;++g_pgraph_custom_methods;return;
    }
    if (method == NV097_DRAW_ARRAYS) {
        if (draw_nv2a_arrays(channel, subchannel, parameter) == 0) {
            g_gpu->pgraph.regs[method / 4u] = parameter;
            return;
        }
        pgraph_method(g_gpu, subchannel, method, parameter);
        return;
    }

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
    if (method == NV097_SET_ALPHA_TEST_ENABLE ||
        method == NV097_SET_DEPTH_TEST_ENABLE ||
        method == NV097_SET_CULL_FACE_ENABLE ||
        method == NV097_SET_DEPTH_FUNC ||
        method == NV097_SET_CULL_FACE ||
        method == NV097_SET_SHADE_MODE ||
        method == NV097_SET_STENCIL_FUNC_REF ||
        method == NV097_SET_FOG_ENABLE ||
        method == NV097_SET_FOG_COLOR ||
        method == NV097_SET_FOG_MODE ||
        method == NV097_SET_LIGHTING_ENABLE ||
        method == NV097_SET_POINT_SIZE) {
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
    memset(g_dma_contexts, 0, sizeof(g_dma_contexts));
    memset(g_primitive_mode, 0, sizeof(g_primitive_mode));
    memset(g_element_indices, 0, sizeof(g_element_indices));
    memset(g_element_count, 0, sizeof(g_element_count));
    memset(g_element_overflow, 0, sizeof(g_element_overflow));
    g_transform_execution_mode = 0;
    memset(g_nv2a_vsh_program, 0, sizeof(g_nv2a_vsh_program));
    memset(g_nv2a_vsh_constants, 0, sizeof(g_nv2a_vsh_constants));
    memset(g_nv2a_vsh_constant_dirty, 0, sizeof(g_nv2a_vsh_constant_dirty));
    g_nv2a_vsh_load_slot = g_nv2a_vsh_load_word = 0;
    g_nv2a_vsh_start_slot = g_nv2a_vsh_constant_load = 0;
    g_nv2a_vsh_constant_word = 0;
    g_nv2a_vsh_context_write = 0;
    g_nv2a_vsh_program_hash = 0;
    if (g_nv2a_vsh_handle) {
        (void)d3d8_vsh_delete_shader(g_nv2a_vsh_handle);
        g_nv2a_vsh_handle = 0;
    }
    g_nv2a_vsh_dirty = 1;
    mark_all_textures_dirty();
    memset(&g_surface_regs, 0, sizeof(g_surface_regs));
    g_pgraph_fallback_methods = 0;
    g_pgraph_custom_methods = 0;
    g_pgraph_custom_draws = 0;
    g_pgraph_custom_vertices = 0;

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
    sync_surface_to_vram();
    release_texture_cache();
    pgraph_d3d11_shutdown();
    release_surface_targets();
    xbox_SetD3D11Device(NULL, NULL);
    xbox_SetD3DDevice(NULL);

    // The upstream NV2A singleton currently has no public destroy/reset API.
    // Keep its backing allocation alive until the host process exits; freeing
    // it here would leave the singleton's VRAM/RAMIN pointers dangling.
    g_started = 0;
    OutputDebugStringW(L"[XboxRuntime:NV2A] Translator stopped. GPU memory retained until process exit.\n");
}

int __cdecl XboxNV2A_SetD3DDevice(void* device)
{
    IDirect3DDevice8* d3d_device = (IDirect3DDevice8*)device;
    if (device && (!d3d_device->lpVtbl ||
        !d3d_device->lpVtbl->AddRef || !d3d_device->lpVtbl->Release))
        return 1;
    xbox_SetD3DDevice(d3d_device);
    if (g_started && d3d_device)
        pgraph_d3d11_init();
    return 0;
}

int __cdecl XboxNV2A_SetD3D11Device(void* device, void* context)
{
    if ((device == NULL) != (context == NULL))
        return 1;
    xbox_SetD3D11Device(device, context);
    if (g_started && device)
        pgraph_d3d11_init();
    return 0;
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

    /* PFIFO CACHE1 DMA mode: a PUT write kicks the validated software pusher. */
    if (register_offset == 0x3240u && access_size == 4u) {
        const uint32_t push1 = (uint32_t)nv2a_mmio_read(g_gpu, 0x3204u, 4u);
        const uint32_t dma_push = (uint32_t)nv2a_mmio_read(g_gpu, 0x3220u, 4u);
        if (((push1 & NV_PFIFO_CACHE1_PUSH1_MODE) >> 8) ==
                NV_PFIFO_CACHE1_PUSH1_MODE_DMA &&
            (dma_push & NV_PFIFO_CACHE1_DMA_PUSH_ACCESS) != 0u) {
            const uint32_t channel = push1 & NV_PFIFO_CACHE1_PUSH1_CHID;
            const uint32_t instance = ((uint32_t)nv2a_mmio_read(
                g_gpu, 0x322Cu, 4u) & NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS) << 4;
            const uint32_t get = (uint32_t)nv2a_mmio_read(g_gpu, 0x3244u, 4u);
            const uint32_t put = (uint32_t)value;
            if (get != put) {
                uint32_t new_get = get, methods = 0;
                const int pusher_result = XboxNV2A_SubmitDMAInstance(
                    channel, instance, get, put, &new_get, &methods);
                if (pusher_result != 0)
                    return 3;
                nv2a_mmio_write(g_gpu, 0x3244u, new_get, 4u);
            }
        }
    }
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

    submit_pgraph_method(0u, subchannel, method, parameter);
    return 0;
}


static int memory_range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return offset <= capacity && length <= capacity - offset;
}


static uint8_t expand5(uint32_t value) { return (uint8_t)((value << 3) | (value >> 2)); }
static uint8_t expand6(uint32_t value) { return (uint8_t)((value << 2) | (value >> 4)); }

static void decode_dxt1_block(const uint8_t* block, uint8_t* dst,
                              uint32_t width, uint32_t height,
                              uint32_t block_x, uint32_t block_y,
                              uint32_t destination_pitch, int force_four_color)
{
    const uint16_t c0 = (uint16_t)(block[0] | (block[1] << 8));
    const uint16_t c1 = (uint16_t)(block[2] | (block[3] << 8));
    uint8_t colors[4][4];
    colors[0][0] = expand5((c0 >> 11) & 31u);
    colors[0][1] = expand6((c0 >> 5) & 63u);
    colors[0][2] = expand5(c0 & 31u); colors[0][3] = 255;
    colors[1][0] = expand5((c1 >> 11) & 31u);
    colors[1][1] = expand6((c1 >> 5) & 63u);
    colors[1][2] = expand5(c1 & 31u); colors[1][3] = 255;
    if (c0 > c1 || force_four_color) {
        for (uint32_t k=0;k<3;k++) {
            colors[2][k]=(uint8_t)((2u*colors[0][k]+colors[1][k])/3u);
            colors[3][k]=(uint8_t)((colors[0][k]+2u*colors[1][k])/3u);
        }
        colors[2][3]=colors[3][3]=255;
    } else {
        for (uint32_t k=0;k<3;k++) colors[2][k]=(uint8_t)((colors[0][k]+colors[1][k])/2u);
        colors[2][3]=255; colors[3][0]=colors[3][1]=colors[3][2]=0; colors[3][3]=0;
    }
    const uint32_t indices=(uint32_t)block[4] | ((uint32_t)block[5]<<8) |
        ((uint32_t)block[6]<<16) | ((uint32_t)block[7]<<24);
    for(uint32_t y=0;y<4;y++) for(uint32_t x=0;x<4;x++) {
        const uint32_t px=block_x*4u+x, py=block_y*4u+y;
        if(px>=width || py>=height) continue;
        const uint32_t index=(indices >> (2u*(y*4u+x))) & 3u;
        memcpy(dst+(size_t)py*destination_pitch+px*4u,colors[index],4u);
    }
}


static void decode_dxt_alpha_block(const uint8_t* block, uint8_t* dst,
    uint32_t width, uint32_t height, uint32_t bx, uint32_t by,
    uint32_t destination_pitch, int dxt5)
{
    uint8_t alpha[8];
    uint64_t bits=0;
    if (!dxt5) {
        for(uint32_t i=0;i<6;i++) bits |= (uint64_t)block[2u+i] << (8u*i);
        for(uint32_t i=0;i<16;i++) {
            const uint32_t x=bx*4u+(i&3u), y=by*4u+(i>>2);
            if(x>=width || y>=height) continue;
            const uint8_t nibble=(uint8_t)((bits>>(4u*i))&15u);
            dst[(size_t)y*destination_pitch+x*4u+3u]=(uint8_t)(nibble*17u);
        }
        return;
    }
    alpha[0]=block[0]; alpha[1]=block[1];
    if(alpha[0]>alpha[1]) {
        for(uint32_t i=1;i<=6;i++) alpha[i+1]=(uint8_t)(((7u-i)*alpha[0]+i*alpha[1])/7u);
    } else {
        for(uint32_t i=1;i<=4;i++) alpha[i+1]=(uint8_t)(((5u-i)*alpha[0]+i*alpha[1])/5u);
        alpha[6]=0; alpha[7]=255;
    }
    for(uint32_t i=0;i<6;i++) bits |= (uint64_t)block[2u+i] << (8u*i);
    for(uint32_t i=0;i<16;i++) {
        const uint32_t x=bx*4u+(i&3u), y=by*4u+(i>>2);
        if(x>=width || y>=height) continue;
        const uint32_t index=(uint32_t)((bits>>(3u*i))&7u);
        dst[(size_t)y*destination_pitch+x*4u+3u]=alpha[index];
    }
}

static uint8_t nv2a_half_to_u8(uint16_t value)
{
    const uint32_t sign = (uint32_t)(value & 0x8000u) << 16;
    uint32_t exponent = (value >> 10) & 0x1Fu;
    uint32_t mantissa = value & 0x03FFu;
    uint32_t bits;
    float converted;
    if (exponent == 0u) {
        if (mantissa == 0u) {
            bits = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x0400u) == 0u) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x03FFu;
            bits = sign | ((uint32_t)(113 - shift) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31u) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    memcpy(&converted, &bits, sizeof(converted));
    if (!(converted > 0.0f))
        return 0u;
    if (converted >= 1.0f)
        return 255u;
    return (uint8_t)(converted * 255.0f + 0.5f);
}

int __cdecl XboxNV2A_DecodeTextureRGBA8(uint32_t format, uint32_t width,
    uint32_t height, uint32_t source_pitch, const void* source,
    uint32_t source_length, void* destination, uint32_t destination_capacity,
    uint32_t* destination_pitch)
{
    if (destination_pitch) *destination_pitch=0;
    if (!width || !height || !source || !destination) return 1;
    const uint32_t color=(format >> 8) & 0xFFu;
    const uint64_t out_pitch=(uint64_t)width*4u;
    const uint64_t out_size=out_pitch*height;
    if(out_pitch>0xFFFFFFFFu || out_size>destination_capacity) return 2;
    uint32_t bytes_per_pixel=0;
    switch(color) {
    case 0x02u: case 0x03u: case 0x04u: case 0x05u:
    case 0x10u: case 0x11u: case 0x17u: case 0x1Au: case 0x20u:
    case 0x1Cu: case 0x1Du: case 0x28u: case 0x29u:
    case 0x2Cu: case 0x30u: case 0x31u: case 0x32u: case 0x35u:
        bytes_per_pixel=2u; break;
    case 0x2Eu: case 0x2Fu:
        bytes_per_pixel=4u; break;
    case 0x06u: case 0x07u: case 0x12u: case 0x1Eu:
    case 0x3Au: case 0x3Bu: case 0x3Cu: case 0x3Fu: case 0x40u: case 0x41u:
        bytes_per_pixel=4u; break;
    case 0x19u: case 0x00u: case 0x13u: case 0x1Fu: case 0x1Bu:
        bytes_per_pixel=1u; break;
    case 0x0Cu: case 0x0Eu: case 0x0Fu: break; /* DXT1/3/5 */
    default: return 3;
    }
    uint64_t min_pitch=0, required=0;
    if(color==0x0Cu || color==0x0Eu || color==0x0Fu) {
        const uint64_t blocks_x=((uint64_t)width+3u)/4u;
        const uint64_t blocks_y=((uint64_t)height+3u)/4u;
        min_pitch=blocks_x*(color==0x0Cu?8u:16u);
        const uint64_t pitch=source_pitch?source_pitch:min_pitch;
        if(pitch<min_pitch || pitch>0xFFFFFFFFu) return 4;
        required=pitch*blocks_y;
        if(required>source_length) return 5;
        const uint8_t* src=(const uint8_t*)source;
        uint8_t* dst=(uint8_t*)destination;
        for(uint32_t by=0;by<(uint32_t)blocks_y;by++)
            for(uint32_t bx=0;bx<(uint32_t)blocks_x;bx++)
                {
                const uint8_t* block=src+(size_t)by*(size_t)pitch+
                    (size_t)bx*(color==0x0Cu?8u:16u);
                if(color==0x0Cu) {
                    decode_dxt1_block(block,dst,width,height,bx,by,
                        (uint32_t)out_pitch,0);
                } else {
                    decode_dxt_alpha_block(block,dst,width,height,bx,by,
                        (uint32_t)out_pitch,color==0x0Fu);
                    decode_dxt1_block(block+8u,dst,width,height,bx,by,
                        (uint32_t)out_pitch,1);
                }
            }
    } else {
        min_pitch=(uint64_t)width*bytes_per_pixel;
        const uint64_t pitch=source_pitch?source_pitch:min_pitch;
        if(pitch<min_pitch || pitch>0xFFFFFFFFu) return 4;
        required=pitch*height;
        if(required>source_length) return 5;
        const uint8_t* src=(const uint8_t*)source;
        uint8_t* dst=(uint8_t*)destination;
        for(uint32_t y=0;y<height;y++) for(uint32_t x=0;x<width;x++) {
            const uint8_t* p=src+(size_t)y*(size_t)pitch+(size_t)x*bytes_per_pixel;
            uint8_t* q=dst+(size_t)y*(size_t)out_pitch+x*4u;
            if(color==0x06u || color==0x07u || color==0x12u || color==0x1Eu) {
                q[0]=p[2]; q[1]=p[1]; q[2]=p[0];
                q[3]=(color==0x06u || color==0x12u)?p[3]:255u;
            } else if(color==0x05u || color==0x11u) {
                const uint16_t v=(uint16_t)(p[0]|(p[1]<<8));
                q[0]=expand5((v>>11)&31u); q[1]=expand6((v>>5)&63u); q[2]=expand5(v&31u); q[3]=255u;
            } else if(color==0x02u || color==0x03u || color==0x04u ||
                      color==0x10u || color==0x1Cu || color==0x1Du) {
                const uint16_t v=(uint16_t)(p[0]|(p[1]<<8));
                if(color==0x04u || color==0x1Du) {
                    q[0]=(uint8_t)(((v>>8)&15u)*17u); q[1]=(uint8_t)(((v>>4)&15u)*17u);
                    q[2]=(uint8_t)((v&15u)*17u); q[3]=(uint8_t)(((v>>12)&15u)*17u);
                } else {
                    q[0]=expand5((v>>10)&31u); q[1]=expand5((v>>5)&31u); q[2]=expand5(v&31u);
                    q[3]=(color==0x02u || color==0x10u)?((v&0x8000u)?255u:0u):255u;
                }
            } else if(color==0x17u || color==0x28u) {
                q[0]=p[0]; q[1]=p[1]; q[2]=p[0]; q[3]=p[1];
            } else if(color==0x29u) {
                q[0]=p[1]; q[1]=p[0]; q[2]=p[0]; q[3]=p[1];
            } else if(color==0x1Au || color==0x20u) {
                q[0]=q[1]=q[2]=p[0]; q[3]=p[1];
            } else if(color==0x1Bu) {
                q[0]=q[1]=q[2]=q[3]=p[0];
            } else if(color==0x2Cu || color==0x30u) {
                q[0]=p[1]; q[1]=0u; q[2]=0u; q[3]=0u;
            } else if(color==0x31u) {
                q[0]=nv2a_half_to_u8((uint16_t)(p[0] | ((uint16_t)p[1] << 8)));
                q[1]=0u; q[2]=255u; q[3]=0u;
            } else if(color==0x2Eu || color==0x2Fu) {
                q[0]=p[3]; q[1]=255u; q[2]=0u; q[3]=0u;
            } else if(color==0x32u || color==0x35u) {
                q[0]=255u; q[1]=p[1]; q[2]=p[0]; q[3]=255u;
            } else if(color==0x3Au || color==0x3Fu) {
                q[0]=p[0]; q[1]=p[1]; q[2]=p[2]; q[3]=p[3];
            } else if(color==0x3Bu || color==0x40u) {
                q[0]=p[1]; q[1]=p[2]; q[2]=p[3]; q[3]=p[0];
            } else if(color==0x3Cu || color==0x41u) {
                q[0]=p[3]; q[1]=p[2]; q[2]=p[1]; q[3]=p[0];
            } else if(color==0x19u || color==0x1Fu) {
                q[0]=q[1]=q[2]=255u; q[3]=p[0];
            } else { q[0]=q[1]=q[2]=p[0]; q[3]=255u; }
        }
    }
    if(destination_pitch) *destination_pitch=(uint32_t)out_pitch;
    return 0;
}


int __cdecl XboxNV2A_DecodeSwizzledTextureRGBA8(uint32_t format,
    uint32_t width, uint32_t height, const void* source,
    uint32_t source_length, void* destination,
    uint32_t destination_capacity, uint32_t* destination_pitch)
{
    if (destination_pitch) *destination_pitch = 0;
    if (!source || !destination || !width || !height ||
        width > 16384u || height > 16384u ||
        (width & (width - 1u)) || (height & (height - 1u)))
        return 1;

    const uint32_t color = (format >> 8) & 0xFFu;
    uint32_t bytes_per_pixel = 0;
    switch (color) {
    case 0x00u: case 0x19u: bytes_per_pixel = 1u; break;
    case 0x02u: case 0x03u: case 0x04u: case 0x05u:
    case 0x1Au: case 0x28u: case 0x29u: case 0x2Cu: case 0x32u:
        bytes_per_pixel = 2u; break;
    case 0x06u: case 0x07u: case 0x3Au: case 0x3Bu: case 0x3Cu:
        bytes_per_pixel = 4u; break;
    default:
        /* Linear-image and block-compressed formats use separate paths. */
        return 2;
    }

    const uint64_t texels = (uint64_t)width * height;
    const uint64_t source_bytes = texels * bytes_per_pixel;
    const uint64_t output_pitch = (uint64_t)width * 4u;
    const uint64_t output_bytes = output_pitch * height;
    if (source_bytes > source_length || source_bytes > XBOX_VRAM_SIZE || source_bytes > SIZE_MAX ||
        output_pitch > UINT32_MAX || output_bytes > destination_capacity)
        return 3;

    uint8_t* linear = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)source_bytes);
    if (!linear) return 4;
    xbox_unswizzle_rect(linear, source, width, height, bytes_per_pixel);
    const uint32_t linear_pitch = width * bytes_per_pixel;
    const int result = XboxNV2A_DecodeTextureRGBA8(
        format, width, height, linear_pitch, linear, (uint32_t)source_bytes,
        destination, destination_capacity, destination_pitch);
    HeapFree(GetProcessHeap(), 0, linear);
    return result;
}


int __cdecl XboxNV2A_DecodeTextureMipChainRGBA8(uint32_t format,
    uint32_t width, uint32_t height, uint32_t level_count,
    const void* source, uint32_t source_length, void* destination,
    uint32_t destination_capacity, uint32_t* level_offsets,
    uint32_t* level_sizes)
{
    if (!source || !destination || !level_offsets || !level_sizes ||
        !width || !height || width > 16384u || height > 16384u ||
        !level_count || level_count > 16u)
        return 1;

    const uint32_t color = (format >> 8) & 0xFFu;
    const int dxt1 = color == 0x0Cu;
    const int dxt_other = color == 0x0Eu || color == 0x0Fu;
    const int linear_image = color == 0x10u || color == 0x11u ||
        color == 0x12u || color == 0x13u || color == 0x17u ||
        color == 0x1Bu || color == 0x1Cu || color == 0x1Du ||
        color == 0x1Eu || color == 0x1Fu || color == 0x20u ||
        color == 0x2Eu || color == 0x2Fu || color == 0x30u ||
        color == 0x31u || color == 0x35u || color == 0x3Fu ||
        color == 0x40u || color == 0x41u;
    uint32_t bytes_per_pixel = 0;
    if (!dxt1 && !dxt_other) {
        switch (color) {
        case 0x00u: case 0x19u: case 0x13u: case 0x1Fu: case 0x1Bu:
            bytes_per_pixel = 1u; break;
        case 0x02u: case 0x03u: case 0x04u: case 0x05u:
        case 0x10u: case 0x11u: case 0x17u: case 0x1Au:
        case 0x1Cu: case 0x1Du: case 0x20u: case 0x28u:
        case 0x29u: case 0x2Cu: case 0x30u: case 0x31u:
        case 0x32u: case 0x35u:
            bytes_per_pixel = 2u; break;
        case 0x2Eu: case 0x2Fu:
            bytes_per_pixel = 4u; break;
        case 0x06u: case 0x07u: case 0x12u: case 0x1Eu:
        case 0x3Au: case 0x3Bu: case 0x3Cu: case 0x3Fu:
        case 0x40u: case 0x41u:
            bytes_per_pixel = 4u; break;
        default: return 2;
        }
        if (linear_image) {
            if (level_count != 1u) return 2;
        } else if ((width & (width - 1u)) || (height & (height - 1u))) {
            return 2;
        }
    }

    /* Validate the entire chain before writing any decoded level. */
    uint64_t source_total = 0, destination_total = 0;
    uint32_t w = width, h = height;
    for (uint32_t level = 0; level < level_count; ++level) {
        uint64_t input_size;
        if (dxt1 || dxt_other) {
            const uint64_t blocks_x = ((uint64_t)w + 3u) / 4u;
            const uint64_t blocks_y = ((uint64_t)h + 3u) / 4u;
            input_size = blocks_x * blocks_y * (dxt1 ? 8u : 16u);
        } else {
            input_size = (uint64_t)w * h * bytes_per_pixel;
        }
        const uint64_t output_size = (uint64_t)w * h * 4u;
        if (input_size > UINT32_MAX || output_size > UINT32_MAX ||
            source_total > source_length || input_size > source_length - source_total ||
            destination_total > destination_capacity ||
            output_size > destination_capacity - destination_total ||
            source_total > UINT32_MAX || destination_total > UINT32_MAX)
            return 3;
        level_offsets[level] = (uint32_t)destination_total;
        level_sizes[level] = (uint32_t)output_size;
        source_total += input_size;
        destination_total += output_size;
        w = w > 1u ? w / 2u : 1u;
        h = h > 1u ? h / 2u : 1u;
    }

    const uint8_t* src = (const uint8_t*)source;
    uint8_t* dst = (uint8_t*)destination;
    uint64_t source_offset = 0;
    uint32_t destination_offset = 0;
    w = width; h = height;
    for (uint32_t level = 0; level < level_count; ++level) {
        uint64_t input_size;
        if (dxt1 || dxt_other) {
            const uint64_t blocks_x = ((uint64_t)w + 3u) / 4u;
            const uint64_t blocks_y = ((uint64_t)h + 3u) / 4u;
            input_size = blocks_x * blocks_y * (dxt1 ? 8u : 16u);
        } else {
            input_size = (uint64_t)w * h * bytes_per_pixel;
        }
        uint32_t pitch = 0;
        const int result = (dxt1 || dxt_other || linear_image)
            ? XboxNV2A_DecodeTextureRGBA8(format, w, h, 0u,
                src + (size_t)source_offset, (uint32_t)input_size,
                dst + destination_offset, destination_capacity - destination_offset, &pitch)
            : XboxNV2A_DecodeSwizzledTextureRGBA8(format, w, h,
                src + (size_t)source_offset, (uint32_t)input_size,
                dst + destination_offset, destination_capacity - destination_offset, &pitch);
        if (result != 0) return 4 + result;
        source_offset += input_size;
        destination_offset += level_sizes[level];
        w = w > 1u ? w / 2u : 1u;
        h = h > 1u ? h / 2u : 1u;
    }
    return 0;
}


int __cdecl XboxNV2A_DecodeTextureMipChainFromDMA(uint32_t channel,
    uint32_t subchannel, uint32_t context_method, uint32_t texture_offset,
    uint32_t format, uint32_t width, uint32_t height, uint32_t level_count,
    void* destination, uint32_t destination_capacity,
    uint32_t* level_offsets, uint32_t* level_sizes)
{
    if (!g_started || !g_gpu || channel > 31u || subchannel > 7u ||
        context_method < 0x0180u || context_method > 0x018Cu ||
        ((context_method - 0x0180u) & 3u) != 0u)
        return 1;

    /* Texture format's two-bit context-DMA selector addresses all four
     * NV097 context slots: DMA_A, DMA_B, DMA_C and DMA_D. */
    const uint32_t slot = (context_method - 0x0180u) / 4u;
    const XboxNV2A_DMA_INFO* dma = &g_dma_contexts[channel][subchannel][slot];
    if (!dma->mapped_length || dma->dma_target != 0u)
        return 2;
    if (texture_offset > dma->mapped_length)
        return 3;

    const uint64_t source_address = (uint64_t)dma->base_address + texture_offset;
    if (source_address > XBOX_VRAM_SIZE)
        return 4;

    const uint32_t available = dma->mapped_length - texture_offset;
    if ((uint64_t)available > (uint64_t)XBOX_VRAM_SIZE - source_address)
        return 5;
    return XboxNV2A_DecodeTextureMipChainRGBA8(
        format, width, height, level_count,
        g_vram + (size_t)source_address, available,
        destination, destination_capacity, level_offsets, level_sizes);
}


int __cdecl XboxNV2A_DecodeTextureFromDMA(uint32_t channel,
    uint32_t subchannel, uint32_t context_method, uint32_t texture_offset,
    uint32_t format, uint32_t width, uint32_t height, uint32_t source_pitch,
    void* destination, uint32_t destination_capacity,
    uint32_t* destination_pitch)
{
    if (!g_started || !g_gpu || channel > 31u || subchannel > 7u ||
        context_method < 0x0180u || context_method > 0x018Cu ||
        ((context_method - 0x0180u) & 3u) != 0u)
        return 1;
    const uint32_t slot = (context_method - 0x0180u) / 4u;
    const XboxNV2A_DMA_INFO* dma = &g_dma_contexts[channel][subchannel][slot];
    if (!dma->mapped_length || dma->dma_target != 0u)
        return 2;
    if (texture_offset > dma->mapped_length)
        return 3;
    const uint64_t source_address = (uint64_t)dma->base_address + texture_offset;
    if (source_address > XBOX_VRAM_SIZE)
        return 4;
    const uint32_t available = dma->mapped_length - texture_offset;
    if ((uint64_t)available > (uint64_t)XBOX_VRAM_SIZE - source_address)
        return 5;
    return XboxNV2A_DecodeTextureRGBA8(format, width, height, source_pitch,
        g_vram + (size_t)source_address, available, destination,
        destination_capacity, destination_pitch);
}



static void mark_all_textures_dirty(void)
{
    for (uint32_t c=0;c<32u;++c) for(uint32_t s=0;s<8u;++s)
        for(uint32_t t=0;t<4u;++t) g_texture_cache[c][s][t].dirty=1;
}
static void mark_context_textures_dirty(uint32_t channel,uint32_t subchannel)
{
    if(channel>31u||subchannel>7u)return;
    for(uint32_t t=0;t<4u;++t)g_texture_cache[channel][subchannel][t].dirty=1;
}
static void release_texture_cache(void)
{
    for(uint32_t c=0;c<32u;++c)for(uint32_t s=0;s<8u;++s)for(uint32_t t=0;t<4u;++t){
        NV2A_TEXTURE_CACHE* e=&g_texture_cache[c][s][t];
        if(e->texture&&e->texture->lpVtbl&&e->texture->lpVtbl->Release)e->texture->lpVtbl->Release(e->texture);
        memset(e,0,sizeof(*e));
    }
}
static int bind_nv2a_textures(uint32_t channel,uint32_t subchannel,IDirect3DDevice8* device)
{
    if(!device||!device->lpVtbl||!device->lpVtbl->SetTexture||!device->lpVtbl->CreateTexture)return 1;
    for(uint32_t stage=0;stage<4u;++stage){
        const uint32_t base=0x1B00u+stage*0x40u;
        const uint32_t control0=g_gpu->pgraph.regs[(base+0x0Cu)/4u];
        NV2A_TEXTURE_CACHE* e=&g_texture_cache[channel][subchannel][stage];
        if(!(control0&(1u<<30))){
            if(FAILED(device->lpVtbl->SetTexture(device,stage,NULL)))return 2;
            continue;
        }
        XboxNV2A_TEXTURE_INFO info;
        if(XboxNV2A_GetTextureInfo(channel,subchannel,stage,&info)!=0)return 3;
        if(!info.level_count||info.level_count>16u)return 4;
        const uint32_t slot=(info.context_method-0x0180u)/4u;
        const XboxNV2A_DMA_INFO* dma=&g_dma_contexts[channel][subchannel][slot];
        const uint32_t control1=g_gpu->pgraph.regs[(base+0x10u)/4u];
        const uint32_t image_rect=g_gpu->pgraph.regs[(base+0x1Cu)/4u];
        const uint32_t color=(info.format>>8)&0xFFu;
        const uint32_t palette=(color==0x0Bu)?g_gpu->pgraph.regs[(base+0x20u)/4u]:0u;
        const uint32_t palette_slot=palette&1u;
        const XboxNV2A_DMA_INFO* palette_dma=&g_dma_contexts[channel][subchannel][palette_slot];
        const int changed=e->dirty||!e->texture||e->owner_device!=device||e->format!=info.format||
            e->offset!=info.texture_offset||e->control0!=control0||
            e->control1!=control1||e->image_rect!=image_rect||
            e->dma_handle!=dma->handle||e->dma_base!=dma->base_address||
            e->dma_length!=dma->mapped_length||e->palette!=palette||
            e->palette_dma_handle!=palette_dma->handle||
            e->palette_dma_base!=palette_dma->base_address||
            e->palette_dma_length!=palette_dma->mapped_length;
        if(changed){
            uint32_t offsets[16]={0},sizes[16]={0},w=info.width,h=info.height;
            uint64_t total=0;
            for(uint32_t level=0;level<info.level_count;++level){
                uint64_t n=(uint64_t)w*h*4u;
                if(!w||!h||n>UINT32_MAX||total+n>64u*1024u*1024u)return 5;
                offsets[level]=(uint32_t)total;sizes[level]=(uint32_t)n;total+=n;
                w=w>1u?w/2u:1u;h=h>1u?h/2u:1u;
            }
            uint8_t* decoded=(uint8_t*)malloc((size_t)total);
            if(!decoded)return 6;
            int dr= XboxNV2A_DecodeTextureStageRGBA8(channel,subchannel,stage,
                decoded,(uint32_t)total,offsets,sizes);
            if(dr){free(decoded);return 7;}
            IDirect3DTexture8* texture=NULL;
            HRESULT hr=device->lpVtbl->CreateTexture(device,info.width,info.height,
                info.level_count,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture);
            if(FAILED(hr)||!texture||!texture->lpVtbl||!texture->lpVtbl->LockRect||
               !texture->lpVtbl->UnlockRect){
                if(texture&&texture->lpVtbl&&texture->lpVtbl->Release)texture->lpVtbl->Release(texture);
                free(decoded);return 8;
            }
            w=info.width;h=info.height;int ok=1;
            for(uint32_t level=0;level<info.level_count;++level){
                D3DLOCKED_RECT locked;memset(&locked,0,sizeof(locked));
                hr=texture->lpVtbl->LockRect(texture,level,&locked,NULL,0);
                uint32_t row_bytes=w*4u;
                if(FAILED(hr)){ok=0;break;}
                if(!locked.pBits||locked.Pitch<(INT)row_bytes){
                    (void)texture->lpVtbl->UnlockRect(texture,level);
                    ok=0;break;
                }
                for(uint32_t y=0;y<h;++y){
                    const uint8_t* src=decoded+offsets[level]+(size_t)y*row_bytes;
                    uint8_t* dst=(uint8_t*)locked.pBits+(size_t)y*(uint32_t)locked.Pitch;
                    for(uint32_t x=0;x<w;++x){
                        dst[x*4u]=src[x*4u+2u];dst[x*4u+1u]=src[x*4u+1u];
                        dst[x*4u+2u]=src[x*4u];dst[x*4u+3u]=src[x*4u+3u];
                    }
                }
                if(FAILED(texture->lpVtbl->UnlockRect(texture,level))){ok=0;break;}
                w=w>1u?w/2u:1u;h=h>1u?h/2u:1u;
            }
            free(decoded);
            if(!ok){texture->lpVtbl->Release(texture);return 9;}
            if(e->texture&&e->texture->lpVtbl&&e->texture->lpVtbl->Release)e->texture->lpVtbl->Release(e->texture);
            e->texture=texture;e->owner_device=device;e->format=info.format;e->offset=info.texture_offset;
            e->control0=control0;e->control1=control1;e->image_rect=image_rect;
            e->dma_handle=dma->handle;e->dma_base=dma->base_address;
            e->dma_length=dma->mapped_length;e->palette=palette;
            e->palette_dma_handle=palette_dma->handle;
            e->palette_dma_base=palette_dma->base_address;
            e->palette_dma_length=palette_dma->mapped_length;e->dirty=0;
        }
        if(FAILED(device->lpVtbl->SetTexture(device,stage,(IDirect3DBaseTexture8*)e->texture)))return 10;
    }
    return 0;
}

static int nv2a_texture_format_is_supported(uint32_t color)
{
    switch (color) {
    case 0x00u: case 0x02u: case 0x03u: case 0x04u: case 0x05u:
    case 0x06u: case 0x07u: case 0x0Bu: case 0x0Cu: case 0x0Eu: case 0x0Fu:
    case 0x10u: case 0x11u: case 0x12u: case 0x13u: case 0x17u:
    case 0x19u: case 0x1Au: case 0x1Bu: case 0x1Cu: case 0x1Du:
    case 0x1Eu: case 0x1Fu: case 0x20u: case 0x28u: case 0x29u:
    case 0x2Cu: case 0x2Eu: case 0x2Fu: case 0x30u: case 0x31u:
    case 0x32u: case 0x35u: case 0x3Au: case 0x3Bu: case 0x3Cu:
    case 0x3Fu: case 0x40u: case 0x41u:
        return 1;
    default:
        return 0;
    }
}

static int nv2a_texture_format_is_linear(uint32_t color)
{
    switch (color) {
    case 0x10u: case 0x11u: case 0x12u: case 0x13u: case 0x17u:
    case 0x1Bu: case 0x1Cu: case 0x1Du: case 0x1Eu: case 0x1Fu:
    case 0x20u: case 0x2Eu: case 0x2Fu: case 0x30u: case 0x31u:
    case 0x35u: case 0x3Fu: case 0x40u: case 0x41u:
        return 1;
    default:
        return 0;
    }
}

int __cdecl XboxNV2A_GetTextureInfo(uint32_t channel, uint32_t subchannel,
    uint32_t stage, XboxNV2A_TEXTURE_INFO* output)
{
    if (!output) return 1;
    memset(output, 0, sizeof(*output));
    if (!g_started || !g_gpu || channel > 31u || subchannel > 7u || stage > 3u)
        return 2;

    const uint32_t base = 0x1B00u + stage * 0x40u;
    const uint32_t offset = g_gpu->pgraph.regs[(base + 0x00u) / 4u];
    const uint32_t format = g_gpu->pgraph.regs[(base + 0x04u) / 4u];
    const uint32_t control0 = g_gpu->pgraph.regs[(base + 0x0Cu) / 4u];
    const uint32_t control1 = g_gpu->pgraph.regs[(base + 0x10u) / 4u];
    const uint32_t image_rect = g_gpu->pgraph.regs[(base + 0x1Cu) / 4u];
    const uint32_t color = (format >> 8) & 0xFFu;
    /* Cubemaps and texture-border layouts need separate layer/edge handling. */
    if (format & 0x0000000Cu) return 6;
    const uint32_t dimensionality = (format >> 4) & 0xFu;
    const uint32_t context_slot = format & 3u;
    /* NV097 texture context selection is DMA_A (0) or DMA_B (1). */
    if (context_slot > 1u) return 7;
    const uint32_t context_method = 0x0180u + context_slot * 4u;
    const uint32_t max_lod = (control0 >> 6) & 0xFFFu;
    const uint32_t encoded_levels = (format >> 16) & 0xFu;
    const int linear = nv2a_texture_format_is_linear(color);
    if (!nv2a_texture_format_is_supported(color)) return 6;
    uint32_t width = 0, height = 0, levels = encoded_levels;
    uint32_t source_pitch = (control1 >> 16) & 0xFFFFu;

    if (dimensionality != 2u || !encoded_levels)
        return 3;
    if (linear) {
        width = (image_rect >> 16) & 0xFFFFu;
        height = image_rect & 0xFFFFu;
        if (!width || !height || width > 16384u || height > 16384u ||
            levels != 1u)
            return 4;
    } else {
        const uint32_t log_width = (format >> 20) & 0xFu;
        const uint32_t log_height = (format >> 24) & 0xFu;
        if (log_width > 14u || log_height > 14u)
            return 4;
        width = 1u << log_width;
        height = 1u << log_height;
        const uint32_t max_dimension_log = log_width > log_height ? log_width : log_height;
        if (levels > max_lod + 1u) levels = max_lod + 1u;
        if (levels > max_dimension_log + 1u) levels = max_dimension_log + 1u;
        if (!levels) return 4;
        source_pitch = 0u;
    }

    const XboxNV2A_DMA_INFO* dma =
        &g_dma_contexts[channel][subchannel][context_slot];
    if (!dma->mapped_length || dma->dma_target != 0u)
        return 5;

    output->texture_offset = offset;
    output->format = format;
    output->width = width;
    output->height = height;
    output->level_count = levels;
    output->source_pitch = source_pitch;
    output->context_method = context_method;
    output->enabled = (control0 & (1u << 30)) != 0u;
    output->linear = (uint32_t)linear;
    return 0;
}


static int decode_paletted_texture_stage(uint32_t channel,uint32_t subchannel,
    uint32_t stage,const XboxNV2A_TEXTURE_INFO* info,void* destination,
    uint32_t destination_capacity,uint32_t* level_offsets,uint32_t* level_sizes)
{
    if(!info||!destination||!level_offsets||!level_sizes||stage>3u||
       !info->level_count||info->level_count>16u)return 1;
    const uint32_t base=0x1B00u+stage*0x40u;
    const uint32_t palette=g_gpu->pgraph.regs[(base+0x20u)/4u];
    const uint32_t palette_slot=palette&1u;
    const uint32_t palette_method=0x0180u+palette_slot*4u;
    const uint32_t palette_length_code=(palette>>2)&3u;
    const uint32_t palette_entries=256u>>palette_length_code;
    const uint32_t palette_bytes=palette_entries*4u;
    const uint32_t palette_offset=palette&0xFFFFFFC0u;
    const XboxNV2A_DMA_INFO* palette_dma=&g_dma_contexts[channel][subchannel][palette_slot];
    const uint32_t texture_slot=(info->context_method-0x0180u)/4u;
    const XboxNV2A_DMA_INFO* texture_dma=&g_dma_contexts[channel][subchannel][texture_slot];
    if(palette_method<0x0180u||!palette_dma->mapped_length||
       palette_dma->dma_target!=0u||!texture_dma->mapped_length||
       texture_dma->dma_target!=0u)return 2;
    if(palette_offset>palette_dma->mapped_length||
       palette_bytes>palette_dma->mapped_length-palette_offset||
       info->texture_offset>texture_dma->mapped_length)return 3;
    const uint64_t palette_address=(uint64_t)palette_dma->base_address+palette_offset;
    const uint64_t texture_address=(uint64_t)texture_dma->base_address+info->texture_offset;
    if(palette_address>XBOX_VRAM_SIZE||palette_bytes>(uint64_t)XBOX_VRAM_SIZE-palette_address||
       texture_address>XBOX_VRAM_SIZE)return 4;

    uint64_t source_total=0,destination_total=0;
    uint32_t w=info->width,h=info->height;
    for(uint32_t level=0;level<info->level_count;++level){
        uint64_t input=(uint64_t)w*h,output=input*4u;
        if(!w||!h||input>UINT32_MAX||output>UINT32_MAX||
           source_total+input>texture_dma->mapped_length-info->texture_offset||
           destination_total+output>destination_capacity||
           source_total+input>64u*1024u*1024u||
           destination_total+output>64u*1024u*1024u)return 5;
        level_offsets[level]=(uint32_t)destination_total;
        level_sizes[level]=(uint32_t)output;
        source_total+=input;destination_total+=output;
        w=w>1u?w/2u:1u;h=h>1u?h/2u:1u;
    }
    if(source_total>(uint64_t)XBOX_VRAM_SIZE-texture_address)return 6;
    uint8_t* linear=(uint8_t*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)source_total);
    if(!linear)return 7;
    const uint8_t* swizzled=g_vram+(size_t)texture_address;
    const uint8_t* palette_data=g_vram+(size_t)palette_address;
    w=info->width;h=info->height;
    uint64_t source_offset=0;
    for(uint32_t level=0;level<info->level_count;++level){
        const size_t level_bytes=(size_t)w*h;
        xbox_unswizzle_rect(linear+(size_t)source_offset,
            swizzled+(size_t)source_offset,w,h,1u);
        for(size_t i=0;i<level_bytes;++i){
            if(linear[(size_t)source_offset+i]>=palette_entries){
                HeapFree(GetProcessHeap(),0,linear);return 8;
            }
        }
        source_offset+=level_bytes;
        w=w>1u?w/2u:1u;h=h>1u?h/2u:1u;
    }
    uint8_t* out=(uint8_t*)destination;
    w=info->width;h=info->height;source_offset=0;
    for(uint32_t level=0;level<info->level_count;++level){
        const size_t count=(size_t)w*h;
        uint8_t* dst=out+level_offsets[level];
        for(size_t i=0;i<count;++i){
            const uint8_t* p=palette_data+(size_t)linear[(size_t)source_offset+i]*4u;
            dst[i*4u+0u]=p[2];dst[i*4u+1u]=p[1];
            dst[i*4u+2u]=p[0];dst[i*4u+3u]=p[3];
        }
        source_offset+=count;
        w=w>1u?w/2u:1u;h=h>1u?h/2u:1u;
    }
    HeapFree(GetProcessHeap(),0,linear);
    return 0;
}

int __cdecl XboxNV2A_DecodeTextureStageRGBA8(uint32_t channel,
    uint32_t subchannel, uint32_t stage, void* destination,
    uint32_t destination_capacity, uint32_t* level_offsets,
    uint32_t* level_sizes)
{
    XboxNV2A_TEXTURE_INFO info;
    if (!destination || !level_offsets || !level_sizes)
        return 1;
    const int info_result = XboxNV2A_GetTextureInfo(
        channel, subchannel, stage, &info);
    if (info_result != 0) return 10 + info_result;
    if (!info.enabled) return 20;
    if (((info.format >> 8) & 0xFFu) == 0x0Bu)
        return decode_paletted_texture_stage(channel,subchannel,stage,&info,
            destination,destination_capacity,level_offsets,level_sizes);
    if (info.linear) {
        const uint64_t decoded_size = (uint64_t)info.width * info.height * 4u;
        if (decoded_size > UINT32_MAX || decoded_size > destination_capacity)
            return 21;
        const int result = XboxNV2A_DecodeTextureFromDMA(
            channel, subchannel, info.context_method, info.texture_offset,
            info.format, info.width, info.height, info.source_pitch,
            destination, destination_capacity, NULL);
        if (result != 0) return 22 + result;
        level_offsets[0] = 0u;
        level_sizes[0] = (uint32_t)decoded_size;
        return 0;
    }
    return XboxNV2A_DecodeTextureMipChainFromDMA(
        channel, subchannel, info.context_method, info.texture_offset,
        info.format, info.width, info.height, info.level_count,
        destination, destination_capacity, level_offsets, level_sizes);
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

static int copy_vram_to_surface(IDirect3DSurface8* surface,
                                     const XboxNV2A_SURFACE_INFO* info)
{
    if (!surface || !surface->lpVtbl || !surface->lpVtbl->LockRect ||
        !surface->lpVtbl->UnlockRect || !info)
        return 0;

    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(surface->lpVtbl->LockRect(surface, &locked, NULL, 0u)))
        return 0;

    const uint32_t row_bytes = info->width * info->color_bytes_per_pixel;
    int ok = locked.pBits != NULL && locked.Pitch >= (INT)row_bytes;
    if (ok) {
        for (uint32_t y = 0; y < info->height; ++y) {
            const uint8_t* source = g_vram + info->color_offset + y * info->color_pitch;
            uint8_t* destination = (uint8_t*)locked.pBits + (size_t)y * (size_t)locked.Pitch;
            memcpy(destination, source, row_bytes);
        }
    }
    const HRESULT unlock_result = surface->lpVtbl->UnlockRect(surface);
    return ok && SUCCEEDED(unlock_result);
}

static int copy_surface_to_vram(IDirect3DSurface8* surface,
                                const XboxNV2A_SURFACE_INFO* info)
{
    if (!surface || !surface->lpVtbl || !surface->lpVtbl->LockRect ||
        !surface->lpVtbl->UnlockRect || !info)
        return 0;

    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(surface->lpVtbl->LockRect(surface, &locked, NULL, D3DLOCK_READONLY)))
        return 0;

    const uint32_t row_bytes = info->width * info->color_bytes_per_pixel;
    int ok = locked.pBits != NULL && locked.Pitch >= (INT)row_bytes;
    if (ok) {
        for (uint32_t y = 0; y < info->height; ++y) {
            const uint8_t* source = (const uint8_t*)locked.pBits + (size_t)y * (size_t)locked.Pitch;
            uint8_t* destination = g_vram + info->color_offset + y * info->color_pitch;
            memcpy(destination, source, row_bytes);
        }
    }
    const HRESULT unlock_result = surface->lpVtbl->UnlockRect(surface);
    return ok && SUCCEEDED(unlock_result);
}

static int copy_vram_to_zeta_surface(IDirect3DSurface8* surface,
                                      const XboxNV2A_SURFACE_INFO* info)
{
    if (!surface || !surface->lpVtbl || !surface->lpVtbl->LockRect ||
        !surface->lpVtbl->UnlockRect || !info ||
        !info->zeta_format || !info->zeta_bytes_per_pixel)
        return 0;

    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(surface->lpVtbl->LockRect(surface, &locked, NULL, 0u)))
        return 0;

    const uint32_t row_bytes = info->width * info->zeta_bytes_per_pixel;
    int ok = locked.pBits != NULL && locked.Pitch >= (INT)row_bytes;
    if (ok) {
        for (uint32_t y = 0; y < info->height; ++y) {
            const uint8_t* source = g_vram + info->zeta_offset +
                                    (size_t)y * info->zeta_pitch;
            uint8_t* destination = (uint8_t*)locked.pBits +
                                   (size_t)y * (size_t)locked.Pitch;
            memcpy(destination, source, row_bytes);
        }
    }
    const HRESULT unlock_result = surface->lpVtbl->UnlockRect(surface);
    return ok && SUCCEEDED(unlock_result);
}

static int copy_zeta_surface_to_vram(IDirect3DSurface8* surface,
                                     const XboxNV2A_SURFACE_INFO* info)
{
    if (!surface || !surface->lpVtbl || !surface->lpVtbl->LockRect ||
        !surface->lpVtbl->UnlockRect || !info ||
        !info->zeta_format || !info->zeta_bytes_per_pixel)
        return 0;

    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(surface->lpVtbl->LockRect(surface, &locked, NULL, D3DLOCK_READONLY)))
        return 0;

    const uint32_t row_bytes = info->width * info->zeta_bytes_per_pixel;
    int ok = locked.pBits != NULL && locked.Pitch >= (INT)row_bytes;
    if (ok) {
        for (uint32_t y = 0; y < info->height; ++y) {
            const uint8_t* source = (const uint8_t*)locked.pBits +
                                    (size_t)y * (size_t)locked.Pitch;
            uint8_t* destination = g_vram + info->zeta_offset +
                                   (size_t)y * info->zeta_pitch;
            memcpy(destination, source, row_bytes);
        }
    }
    const HRESULT unlock_result = surface->lpVtbl->UnlockRect(surface);
    return ok && SUCCEEDED(unlock_result);
}

static void sync_surface_to_vram(void)
{
    if (!g_surface_bound)
        return;

    if (g_color_surface &&
        copy_surface_to_vram(g_color_surface, &g_bound_surface_info))
        mark_all_textures_dirty();

    if (g_zeta_surface)
        (void)copy_zeta_surface_to_vram(g_zeta_surface, &g_bound_surface_info);
}

static void release_surface_targets(void)
{
    if (g_color_surface && g_color_surface->lpVtbl && g_color_surface->lpVtbl->Release)
        g_color_surface->lpVtbl->Release(g_color_surface);
    if (g_zeta_surface && g_zeta_surface->lpVtbl && g_zeta_surface->lpVtbl->Release)
        g_zeta_surface->lpVtbl->Release(g_zeta_surface);
    g_color_surface = NULL;
    g_zeta_surface = NULL;
    g_surface_bound = 0;
    memset(&g_bound_surface_info, 0, sizeof(g_bound_surface_info));
}

static int update_surface_target(void)
{
    if (!g_surface_regs.dirty)
        return 0;

    IDirect3DDevice8* device = xbox_GetD3DDevice();
    if (!device || !device->lpVtbl ||
        !device->lpVtbl->CreateRenderTarget ||
        !device->lpVtbl->SetRenderTarget)
        return 1;

    XboxNV2A_SURFACE_INFO info;
    int result = XboxNV2A_DecodeSurface(
        g_surface_regs.format, g_surface_regs.pitch,
        g_surface_regs.color_offset, g_surface_regs.zeta_offset,
        g_surface_regs.clip_horizontal, g_surface_regs.clip_vertical, &info);
    if (result != 0) return 2 + result;

    if (g_surface_bound &&
        memcmp(&info, &g_bound_surface_info, sizeof(info)) == 0) {
        g_surface_regs.dirty = 0;
        return 0;
    }

    IDirect3DSurface8* new_color = NULL;
    IDirect3DSurface8* new_zeta = NULL;
    HRESULT hr = device->lpVtbl->CreateRenderTarget(
        device, info.width, info.height, (D3DFORMAT)info.color_format,
        D3DMULTISAMPLE_NONE, TRUE, &new_color);
    if (FAILED(hr) || !new_color)
        return 16;

    if (info.zeta_format) {
        if (!device->lpVtbl->CreateDepthStencilSurface) {
            new_color->lpVtbl->Release(new_color);
            return 17;
        }
        hr = device->lpVtbl->CreateDepthStencilSurface(
            device, info.width, info.height, (D3DFORMAT)info.zeta_format,
            D3DMULTISAMPLE_NONE, &new_zeta);
        if (FAILED(hr) || !new_zeta) {
            new_color->lpVtbl->Release(new_color);
            return 18;
        }
    }

    if (g_surface_bound && g_color_surface &&
        !copy_surface_to_vram(g_color_surface, &g_bound_surface_info)) {
        if (new_zeta) new_zeta->lpVtbl->Release(new_zeta);
        new_color->lpVtbl->Release(new_color);
        return 19;
    }
    if (g_surface_bound && g_zeta_surface)
        (void)copy_zeta_surface_to_vram(g_zeta_surface, &g_bound_surface_info);
    if (g_surface_bound && g_color_surface)
        mark_all_textures_dirty();

    if (!copy_vram_to_surface(new_color, &info)) {
        if (new_zeta) new_zeta->lpVtbl->Release(new_zeta);
        new_color->lpVtbl->Release(new_color);
        return 20;
    }
    /* Some D3D8 backends expose depth surfaces as non-lockable. Preserve
     * render-target binding in that case; depth upload/readback is best-effort
     * and must not make an otherwise valid color target unusable. */
    if (new_zeta)
        (void)copy_vram_to_zeta_surface(new_zeta, &info);

    hr = device->lpVtbl->SetRenderTarget(device, new_color, new_zeta);
    if (FAILED(hr)) {
        if (new_zeta) new_zeta->lpVtbl->Release(new_zeta);
        new_color->lpVtbl->Release(new_color);
        return 21;
    }

    IDirect3DSurface8* old_color = g_color_surface;
    IDirect3DSurface8* old_zeta = g_zeta_surface;
    g_color_surface = new_color;
    g_zeta_surface = new_zeta;
    g_bound_surface_info = info;
    g_surface_bound = 1;
    g_surface_regs.dirty = 0;

    if (old_color && old_color->lpVtbl && old_color->lpVtbl->Release)
        old_color->lpVtbl->Release(old_color);
    if (old_zeta && old_zeta->lpVtbl && old_zeta->lpVtbl->Release)
        old_zeta->lpVtbl->Release(old_zeta);
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
    if (length) { memcpy(g_vram + offset, source, length); mark_all_textures_dirty(); }
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

static int resolve_dma_instance(uint32_t instance, uint32_t handle,
                                uint32_t engine, XboxNV2A_DMA_INFO* output)
{
    if (!output) return 1;
    memset(output, 0, sizeof(*output));
    if ((instance & 0xFu) || !memory_range_valid(instance, 12u, XBOX_RAMIN_SIZE))
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
    if (target != 0u) return 6;
    if (base >= XBOX_VRAM_SIZE || limit > XBOX_VRAM_SIZE - base)
        return 7;

    output->handle = handle;
    output->instance_offset = instance;
    output->dma_class = dma_class;
    output->dma_target = target;
    output->engine = engine;
    output->base_address = base;
    output->limit = limit;
    output->mapped_length = limit;
    return 0;
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
    const uint32_t engine = (context & 0x00030000u) >> 16;
    return resolve_dma_instance(instance, handle, engine, output);
}


int __cdecl XboxNV2A_GetDMAContext(uint32_t channel, uint32_t subchannel,
                                         uint32_t context_method,
                                         XboxNV2A_DMA_INFO* output)
{
    if (!output) return 1;
    memset(output, 0, sizeof(*output));
    if (!g_started || channel > 31u || subchannel > 7u ||
        context_method < 0x0180u || context_method > 0x01FCu ||
        (context_method & 3u))
        return 2;

    const XboxNV2A_DMA_INFO* info =
        &g_dma_contexts[channel][subchannel][(context_method - 0x0180u) / 4u];
    if (!info->mapped_length) return 3;
    *output = *info;
    return 0;
}

int __cdecl XboxNV2A_ReadVertexArray(uint32_t channel, uint32_t subchannel,
    uint32_t array_index, uint32_t first_vertex, uint32_t vertex_count,
    void* destination, uint32_t destination_capacity,
    uint32_t* stride_out, uint32_t* element_bytes_out)
{
    if (stride_out) *stride_out = 0;
    if (element_bytes_out) *element_bytes_out = 0;
    if (!g_started || !g_gpu || channel > 31u || subchannel > 7u ||
        array_index > 15u || (!destination && vertex_count != 0u))
        return 1;

    const uint32_t context_method = array_index < 8u ? 0x019Cu : 0x01A0u;
    const uint32_t context_slot = (context_method - 0x0180u) / 4u;
    const XboxNV2A_DMA_INFO* dma = &g_dma_contexts[channel][subchannel][context_slot];
    if (!dma->mapped_length || dma->dma_target != 0u)
        return 2;

    const uint32_t offset_method = 0x1720u + array_index * 4u;
    const uint32_t format_method = 0x1760u + array_index * 4u;
    const uint32_t offset = g_gpu->pgraph.regs[offset_method / 4u];
    const uint32_t format = g_gpu->pgraph.regs[format_method / 4u];
    const uint32_t type = format & 0xFu;
    const uint32_t components = (format >> 4) & 0xFu;
    uint32_t component_bytes = 0;

    if (!components || components > 4u) return 3;
    switch (type) {
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D:
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL:
        component_bytes = 1u; break;
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1:
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K:
        component_bytes = 2u; break;
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP:
        if (array_index != 2u || components != 3u) return 4;
        component_bytes = 4u; break;
    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
        component_bytes = 4u; break;
    default:
        return 4; /* Compressed and reserved vertex encodings need their own decoder. */
    }

    const uint32_t element_bytes =
        type == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP ? 4u :
        components * component_bytes;
    uint32_t stride = (format & 0xFFFFFF00u) >> 8;
    if (!stride) stride = element_bytes;
    if (stride < element_bytes) return 5;
    if (stride_out) *stride_out = stride;
    if (element_bytes_out) *element_bytes_out = element_bytes;
    if (!vertex_count) return 0;

    const uint64_t output_bytes = (uint64_t)vertex_count * element_bytes;
    if (output_bytes > destination_capacity) return 6;
    const uint64_t first_offset = (uint64_t)offset +
                                  (uint64_t)first_vertex * stride;
    const uint64_t source_span = (uint64_t)(vertex_count - 1u) * stride +
                                 element_bytes;
    if (first_offset > dma->mapped_length ||
        source_span > (uint64_t)dma->mapped_length - first_offset)
        return 7;

    const uint64_t gpu_offset = (uint64_t)dma->base_address + first_offset;
    if (gpu_offset > XBOX_VRAM_SIZE ||
        source_span > (uint64_t)XBOX_VRAM_SIZE - gpu_offset)
        return 8;

    uint8_t* output = (uint8_t*)destination;
    for (uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
        const uint64_t source_offset = gpu_offset + (uint64_t)vertex * stride;
        memcpy(output + (size_t)vertex * element_bytes,
               g_vram + (size_t)source_offset, element_bytes);
    }
    return 0;
}

static int read_vertex_float_array(uint32_t channel,uint32_t subchannel,
    uint32_t array_index,uint32_t first_vertex,uint32_t vertex_count,
    uint32_t expected_components,float* destination,uint32_t destination_capacity)
{
    if(!destination||!vertex_count||vertex_count>255u)return 1;
    const uint32_t format=g_gpu->pgraph.regs[(0x1760u+array_index*4u)/4u];
    const uint32_t type=format&0xFu,components=(format>>4)&0xFu;
    if(components!=expected_components||
       (type!=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F&&
        type!=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1&&
        type!=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K&&
        type!=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP))return 2;
    if(type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP&&
       (array_index!=2u||expected_components!=3u))return 2;
    const uint32_t bytes_per_component=(type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F||
        type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP)?4u:2u;
    uint8_t raw[255u*4u*4u];uint32_t stride=0,element_bytes=0;
    int result=XboxNV2A_ReadVertexArray(channel,subchannel,array_index,
        first_vertex,vertex_count,raw,sizeof(raw),&stride,&element_bytes);
    const uint32_t expected_raw_bytes=
        type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP?4u:
        components*bytes_per_component;
    if(result!=0||element_bytes!=expected_raw_bytes)return 3;
    if((uint64_t)vertex_count*components*sizeof(float)>destination_capacity)return 4;
    if(type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP){
        for(uint32_t v=0;v<vertex_count;++v){
            uint32_t packed;memcpy(&packed,raw+(size_t)v*4u,4u);
            int32_t x=(int32_t)(packed&0x7FFu);if(x&0x400)x-=0x800;
            int32_t y=(int32_t)((packed>>11)&0x7FFu);if(y&0x400)y-=0x800;
            int32_t z=(int32_t)((packed>>22)&0x3FFu);if(z&0x200)z-=0x400;
            float nx=(float)x/1023.0f,ny=(float)y/1023.0f,nz=(float)z/511.0f;
            destination[v*3u+0u]=nx < -1.0f ? -1.0f : (nx > 1.0f ? 1.0f : nx);
            destination[v*3u+1u]=ny < -1.0f ? -1.0f : (ny > 1.0f ? 1.0f : ny);
            destination[v*3u+2u]=nz < -1.0f ? -1.0f : (nz > 1.0f ? 1.0f : nz);
        }
    }else for(uint32_t i=0;i<vertex_count*components;++i){
        if(type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F)
            memcpy(&destination[i],raw+(size_t)i*4u,4u);
        else{
            int16_t value;memcpy(&value,raw+(size_t)i*2u,2u);
            if(type==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1){
                float normalized=(float)value/32767.0f;
                destination[i]=normalized < -1.0f ? -1.0f : normalized;
            }else destination[i]=(float)value;
        }
    }
    return 0;
}

int __cdecl XboxNV2A_BuildVertexStream(uint32_t channel, uint32_t subchannel,
    uint32_t first_vertex, uint32_t vertex_count,
    XboxNV2A_VERTEX* destination, uint32_t destination_capacity,
    uint32_t* stride_out, uint32_t* fvf_out)
{
    if (stride_out) *stride_out = 0;
    if (fvf_out) *fvf_out = 0;
    if (!g_started || !g_gpu || channel > 31u || subchannel > 7u ||
        vertex_count > 255u || (!destination && vertex_count != 0u))
        return 1;

    const uint64_t required = (uint64_t)vertex_count * sizeof(XboxNV2A_VERTEX);
    if (required > destination_capacity) return 2;
    if (!vertex_count) return 0;

    const uint32_t position_format = g_gpu->pgraph.regs[0x1760u / 4u];
    if (((position_format >> 4) & 0xFu) != 3u) return 3;

    float positions[255u * 3u];
    float normals[255u * 3u];
    uint32_t colors[255u];
    uint32_t specular[255u];
    float texcoords[255u * 2u];
    float texcoords1[255u * 2u];
    float texcoords2[255u * 2u];
    float texcoords3[255u * 2u];
    uint32_t stride = 0, element_bytes = 0;
    int result=read_vertex_float_array(channel,subchannel,0u,
        first_vertex,vertex_count,3u,positions,sizeof(positions));
    if(result!=0)return 4;

    int has_normal = 0;
    const uint32_t normal_format = g_gpu->pgraph.regs[0x1768u / 4u];
    if (normal_format != 0u) {
        if (((normal_format >> 4) & 0xFu) != 3u) return 5;
        result=read_vertex_float_array(channel,subchannel,2u,
            first_vertex,vertex_count,3u,normals,sizeof(normals));
        if(result!=0)return 6;
        has_normal = 1;
    }

    int has_color = 0;
    const uint32_t color_format = g_gpu->pgraph.regs[0x176Cu / 4u];
    if (color_format != 0u) {
        const uint32_t color_type = color_format & 0xFu;
        if ((color_type != NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D &&
             color_type != NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL) ||
            ((color_format >> 4) & 0xFu) != 4u)
            return 5;
        result = XboxNV2A_ReadVertexArray(channel, subchannel, 3u,
            first_vertex, vertex_count, colors, sizeof(colors),
            &stride, &element_bytes);
        if (result != 0 || element_bytes != 4u) return 7;
        if (color_type == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL) {
            for (uint32_t i = 0; i < vertex_count; ++i)
                colors[i] = (colors[i] & 0xFF00FF00u) |
                    ((colors[i] & 0x000000FFu) << 16) |
                    ((colors[i] & 0x00FF0000u) >> 16);
        }
        has_color = 1;
    }

    int has_specular = 0;
    const uint32_t specular_format = g_gpu->pgraph.regs[0x1770u / 4u];
    if (specular_format != 0u) {
        const uint32_t specular_type = specular_format & 0xFu;
        if ((specular_type != NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D &&
             specular_type != NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL) ||
            ((specular_format >> 4) & 0xFu) != 4u)
            return 8;
        result = XboxNV2A_ReadVertexArray(channel, subchannel, 4u,
            first_vertex, vertex_count, specular, sizeof(specular),
            &stride, &element_bytes);
        if (result != 0 || element_bytes != 4u) return 9;
        if (specular_type == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL) {
            for (uint32_t i = 0; i < vertex_count; ++i)
                specular[i] = (specular[i] & 0xFF00FF00u) |
                    ((specular[i] & 0x000000FFu) << 16) |
                    ((specular[i] & 0x00FF0000u) >> 16);
        }
        has_specular = 1;
    }
    int has_texcoord = 0;
    const uint32_t tex_format = g_gpu->pgraph.regs[0x177Cu / 4u];
    if (tex_format != 0u) {
        if(((tex_format>>4)&0xFu)!=2u)return 7;
        result=read_vertex_float_array(channel,subchannel,7u,
            first_vertex,vertex_count,2u,texcoords,sizeof(texcoords));
        if(result!=0)return 10;
        has_texcoord = 1;
    }

    int has_texcoord1 = 0;
    const uint32_t tex1_format = g_gpu->pgraph.regs[0x1780u / 4u];
    if (tex1_format != 0u) {
        if(((tex1_format>>4)&0xFu)!=2u)return 11;
        result=read_vertex_float_array(channel,subchannel,8u,
            first_vertex,vertex_count,2u,texcoords1,sizeof(texcoords1));
        if(result!=0)return 12;
        has_texcoord1 = 1;
    }

    int has_texcoord2 = 0;
    const uint32_t tex2_format = g_gpu->pgraph.regs[0x1784u / 4u];
    if (tex2_format != 0u) {
        if(((tex2_format>>4)&0xFu)!=2u)return 13;
        result=read_vertex_float_array(channel,subchannel,9u,
            first_vertex,vertex_count,2u,texcoords2,sizeof(texcoords2));
        if(result!=0)return 14;
        has_texcoord2 = 1;
    }

    int has_texcoord3 = 0;
    const uint32_t tex3_format = g_gpu->pgraph.regs[0x1788u / 4u];
    if (tex3_format != 0u) {
        if(((tex3_format>>4)&0xFu)!=2u)return 15;
        result=read_vertex_float_array(channel,subchannel,10u,
            first_vertex,vertex_count,2u,texcoords3,sizeof(texcoords3));
        if(result!=0)return 16;
        has_texcoord3 = 1;
    }

    for (uint32_t i = 0; i < vertex_count; ++i) {
        XboxNV2A_VERTEX* vertex = &destination[i];
        vertex->x = positions[i * 3u + 0u];
        vertex->y = positions[i * 3u + 1u];
        vertex->z = positions[i * 3u + 2u];
        vertex->nx = has_normal ? normals[i * 3u + 0u] : 0.0f;
        vertex->ny = has_normal ? normals[i * 3u + 1u] : 0.0f;
        vertex->nz = has_normal ? normals[i * 3u + 2u] : 1.0f;
        vertex->diffuse = has_color ? colors[i] : 0xFFFFFFFFu;
        vertex->specular = has_specular ? specular[i] : 0u;
        vertex->u = has_texcoord ? texcoords[i * 2u + 0u] : 0.0f;
        vertex->v = has_texcoord ? texcoords[i * 2u + 1u] : 0.0f;
        vertex->u1 = has_texcoord1 ? texcoords1[i * 2u + 0u] : 0.0f;
        vertex->v1 = has_texcoord1 ? texcoords1[i * 2u + 1u] : 0.0f;
        vertex->u2 = has_texcoord2 ? texcoords2[i * 2u + 0u] : 0.0f;
        vertex->v2 = has_texcoord2 ? texcoords2[i * 2u + 1u] : 0.0f;
        vertex->u3 = has_texcoord3 ? texcoords3[i * 2u + 0u] : 0.0f;
        vertex->v3 = has_texcoord3 ? texcoords3[i * 2u + 1u] : 0.0f;
    }

    if (stride_out) *stride_out = (uint32_t)sizeof(XboxNV2A_VERTEX);
    if (fvf_out) *fvf_out = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX4;
    return 0;
}


static int nv2a_prepare_gpu_vertex_shader(IDirect3DDevice8* device)
{
    if (!device || !device->lpVtbl || !device->lpVtbl->SetVertexShader)
        return 0;
    /* Context-write programs modify the NV2A constant file as a side effect.
     * The D3D11 translator does not implement that side effect yet, so reject
     * them rather than silently producing incorrect GPU results or falling
     * back to CPU vertex execution. */
    if (g_nv2a_vsh_context_write)
        return 0;
    if (g_nv2a_vsh_dirty || !g_nv2a_vsh_handle) {
        uint32_t instruction_count = 0;
        XboxNV2A_VERTEX_PROGRAM_INFO metadata;
        if (g_nv2a_vsh_start_slot >= 136u)
            return 0;
        for (uint32_t slot = g_nv2a_vsh_start_slot; slot < 136u; ++slot) {
            ++instruction_count;
            if (g_nv2a_vsh_program[slot][3] & 1u)
                break;
        }
        if (!instruction_count ||
            !(g_nv2a_vsh_program[g_nv2a_vsh_start_slot + instruction_count - 1u][3] & 1u))
            return 0;
        if (XboxNV2A_ValidateVertexProgram(
                &g_nv2a_vsh_program[g_nv2a_vsh_start_slot][0],
                instruction_count, &metadata) != 0)
            return 0;
        g_nv2a_vsh_input_mask = metadata.input_mask;
        uint32_t hash = 2166136261u;
        const uint32_t* words = &g_nv2a_vsh_program[g_nv2a_vsh_start_slot][0];
        for (uint32_t i = 0; i < instruction_count * 4u; ++i)
            hash = (hash ^ words[i]) * 16777619u;

        /* Writes to inactive slots must not churn the active shader handle. */
        if (g_nv2a_vsh_handle && hash == g_nv2a_vsh_program_hash) {
            g_nv2a_vsh_dirty = 0;
        } else {
            if (g_nv2a_vsh_handle) {
                (void)d3d8_vsh_delete_shader(g_nv2a_vsh_handle);
                g_nv2a_vsh_handle = 0;
            }
            if (FAILED(d3d8_vsh_create_shader(
                    words, (int)instruction_count, &g_nv2a_vsh_handle)))
                return 0;
            g_nv2a_vsh_program_hash = hash;
            g_nv2a_vsh_dirty = 0;
        }
    }
    /* D3D8's D3D11-backed implementation translates the microcode to HLSL,
     * compiles it on cache miss, and binds the cached GPU vertex shader. */
    return SUCCEEDED(device->lpVtbl->SetVertexShader(device, g_nv2a_vsh_handle));
}

/* Match d3d8_vsh.c's ATTR input-layout order exactly. CPU work here is
 * register-to-stream staging only; all vertex instructions run on D3D11. */
static int nv2a_pack_gpu_vertex_stream(const XboxNV2A_VERTEX* vertices,
    uint32_t vertex_count, uint16_t input_mask,
    const uint8_t** output, uint32_t* stride_out)
{
    uint32_t offsets[16] = {0}, sizes[16] = {0}, stride = 0;
    size_t required;
    if (output) *output = NULL;
    if (stride_out) *stride_out = 0;
    if (!vertices || !vertex_count || !output || !stride_out)
        return 0;

    for (uint32_t reg = 0; reg < 16u; ++reg) {
        if (!(input_mask & (uint16_t)(1u << reg)))
            continue;
        offsets[reg] = stride;
        switch (reg) {
        case 0u: case 2u: sizes[reg] = 12u; break;
        case 3u: case 4u: case 7u: sizes[reg] = 4u; break;
        case 5u: case 6u: sizes[reg] = 4u; break;
        case 8u: case 9u: case 10u: case 11u: sizes[reg] = 8u; break;
        default: sizes[reg] = 16u; break;
        }
        if (stride > UINT32_MAX - sizes[reg]) return 0;
        stride += sizes[reg];
    }
    if (!stride || vertex_count > SIZE_MAX / stride)
        return 0;
    required = (size_t)vertex_count * stride;
    if (required > g_nv2a_gpu_vertex_capacity) {
        uint8_t* grown = (uint8_t*)realloc(g_nv2a_gpu_vertex_stream, required);
        if (!grown) return 0;
        g_nv2a_gpu_vertex_stream = grown;
        g_nv2a_gpu_vertex_capacity = required;
    }

    for (uint32_t i = 0; i < vertex_count; ++i) {
        for (uint32_t reg = 0; reg < 16u; ++reg) {
            if (!(input_mask & (uint16_t)(1u << reg)))
                continue;
            uint8_t* dst = g_nv2a_gpu_vertex_stream +
                (size_t)i * stride + offsets[reg];
            const XboxNV2A_VERTEX* v = &vertices[i];
            switch (reg) {
            case 0u: memcpy(dst, &v->x, 12u); break;
            case 1u: {
                const float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
                memcpy(dst, weights, sizeof(weights)); break;
            }
            case 2u: memcpy(dst, &v->nx, 12u); break;
            case 3u: memcpy(dst, &v->diffuse, 4u); break;
            case 4u: memcpy(dst, &v->specular, 4u); break;
            case 5u: case 6u: {
                const float zero = 0.0f;
                memcpy(dst, &zero, sizeof(zero)); break;
            }
            case 7u: {
                const uint32_t opaque_black = 0xFF000000u;
                memcpy(dst, &opaque_black, sizeof(opaque_black)); break;
            }
            case 8u: memcpy(dst, &v->u, 8u); break;
            case 9u: memcpy(dst, &v->u1, 8u); break;
            case 10u: memcpy(dst, &v->u2, 8u); break;
            case 11u: memcpy(dst, &v->u3, 8u); break;
            default: {
                const float generic[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                memcpy(dst, generic, sizeof(generic)); break;
            }
            }
        }
    }
    *output = g_nv2a_gpu_vertex_stream;
    *stride_out = stride;
    return 1;
}

static void nv2a_flush_vertex_constants(void)
{
    uint32_t reg = 0;
    while (reg < 192u) {
        if (!(g_nv2a_vsh_constant_dirty[reg >> 6] &
              ((uint64_t)1u << (reg & 63u)))) {
            ++reg;
            continue;
        }
        const uint32_t first = reg;
        do {
            g_nv2a_vsh_constant_dirty[reg >> 6] &=
                ~((uint64_t)1u << (reg & 63u));
            ++reg;
        } while (reg < 192u &&
                 (g_nv2a_vsh_constant_dirty[reg >> 6] &
                  ((uint64_t)1u << (reg & 63u))));
        d3d8_vsh_set_constant((int)first,
            &g_nv2a_vsh_constants[first][0], (int)(reg - first));
    }
}


static int draw_nv2a_indexed(uint32_t channel,uint32_t subchannel,const uint32_t* indices,uint32_t count)
{
    const uint32_t mode=g_primitive_mode[channel][subchannel];
    if(!indices||!count||count>NV2A_MAX_BATCH_VERTICES||mode==NV097_SET_BEGIN_END_OP_END)return 1;
    XboxNV2A_VERTEX v[NV2A_MAX_BATCH_VERTICES],x[NV2A_MAX_BATCH_VERTICES * 2u]; uint32_t stride=0,fvf=0;
    if(XboxNV2A_BuildIndexedVertexStream(channel,subchannel,indices,count,v,sizeof(v),&stride,&fvf)!=0)return 2;
    const XboxNV2A_VERTEX* draw=v; uint32_t draw_count=count,pc=0; D3DPRIMITIVETYPE pt=D3DPT_TRIANGLELIST;
    switch(mode){
    case NV097_SET_BEGIN_END_OP_POINTS: pt=D3DPT_POINTLIST;pc=count;break;
    case NV097_SET_BEGIN_END_OP_LINES: if(count<2u||(count&1u))return 3;pt=D3DPT_LINELIST;pc=count/2u;break;
    case NV097_SET_BEGIN_END_OP_LINE_STRIP: if(count<2u)return 3;pt=D3DPT_LINESTRIP;pc=count-1u;break;
    case NV097_SET_BEGIN_END_OP_LINE_LOOP: if(count<2u)return 3;memcpy(x,v,count*sizeof(*v));x[count]=v[0];draw=x;draw_count=count+1u;pt=D3DPT_LINESTRIP;pc=count;break;
    case NV097_SET_BEGIN_END_OP_TRIANGLES: if(count<3u||count%3u)return 3;pt=D3DPT_TRIANGLELIST;pc=count/3u;break;
    case NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP: if(count<3u)return 3;pt=D3DPT_TRIANGLESTRIP;pc=count-2u;break;
    case NV097_SET_BEGIN_END_OP_TRIANGLE_FAN: case NV097_SET_BEGIN_END_OP_POLYGON: if(count<3u)return 3;pt=D3DPT_TRIANGLEFAN;pc=count-2u;break;
    case NV097_SET_BEGIN_END_OP_QUADS:
        if(count<4u||count%4u)return 3;draw_count=count/4u*6u;
        for(uint32_t q=0,o=0;q<count;q+=4u){x[o++]=v[q];x[o++]=v[q+1];x[o++]=v[q+2];x[o++]=v[q];x[o++]=v[q+2];x[o++]=v[q+3];}
        draw=x;pt=D3DPT_TRIANGLELIST;pc=draw_count/3u;break;
    case NV097_SET_BEGIN_END_OP_QUAD_STRIP:
        if(count<4u||(count&1u))return 3;{uint32_t quads=count/2u-1u;draw_count=quads*6u;
        for(uint32_t q=0,o=0;q<quads;++q){uint32_t i=q*2u;x[o++]=v[i];x[o++]=v[i+1];x[o++]=v[i+3];x[o++]=v[i];x[o++]=v[i+3];x[o++]=v[i+2];}}
        draw=x;pt=D3DPT_TRIANGLELIST;pc=draw_count/3u;break;
    default:return 4;
    }
    IDirect3DDevice8* d=xbox_GetD3DDevice();
    if(!d||!d->lpVtbl||!d->lpVtbl->SetVertexShader||!d->lpVtbl->DrawPrimitiveUP)return 5;
    const void *draw_data = draw;
    uint32_t draw_stride = stride;
    if ((g_transform_execution_mode & 3u) == 2u) {
        /* Select the FVF so the D3D11 translator builds the matching ATTR
         * input layout, then bind the cached programmable vertex shader. */
        if (FAILED(d->lpVtbl->SetVertexShader(d, fvf)))
            return 6;
        nv2a_flush_vertex_constants();
        if (!nv2a_prepare_gpu_vertex_shader(d))
            return 9;
        const uint8_t* packed = NULL;
        if (!nv2a_pack_gpu_vertex_stream(draw, draw_count,
                g_nv2a_vsh_input_mask, &packed, &draw_stride))
            return 10;
        draw_data = packed;
    } else if (FAILED(d->lpVtbl->SetVertexShader(d,fvf))) {
        return 6;
    }
    if(bind_nv2a_textures(channel,subchannel,d)!=0)return 8;
    if(FAILED(d->lpVtbl->DrawPrimitiveUP(d,pt,pc,draw_data,draw_stride)))return 7;
    ++g_pgraph_custom_methods;++g_pgraph_custom_draws;g_pgraph_custom_vertices+=count;(void)draw_count;return 0;
}
static int draw_nv2a_arrays(uint32_t channel,uint32_t subchannel,uint32_t parameter)
{
    uint32_t count=(parameter&NV097_DRAW_ARRAYS_COUNT)>>24,first=parameter&NV097_DRAW_ARRAYS_START_INDEX;
    if(!count||count>NV2A_MAX_BATCH_VERTICES)return 1;uint32_t indices[NV2A_MAX_BATCH_VERTICES];
    for(uint32_t i=0;i<count;++i){if(first>UINT32_MAX-i)return 1;indices[i]=first+i;}
    return draw_nv2a_indexed(channel,subchannel,indices,count);
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
        submit_pgraph_method(channel, subchannel, 0u, instance);
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
        XboxNV2A_DMA_INFO dma;
        if (XboxNV2A_ResolveDMAHandle(channel, parameter, &dma) != 0)
            return 5;
        g_dma_contexts[channel][subchannel][(method - 0x0180u) / 4u] = dma;
        mark_context_textures_dirty(channel,subchannel);
        parameter = dma.instance_offset;
        if (!memory_range_valid(parameter, 4u, XBOX_RAMIN_SIZE))
            return 6;
    }

    submit_pgraph_method(channel, subchannel, method, parameter);
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
        XboxNV2A_DMA_INFO dma;
        if (XboxNV2A_ResolveDMAHandle(channel, parameter, &dma) != 0)
            return 4;
        if (!memory_range_valid(dma.instance_offset, 12u, XBOX_RAMIN_SIZE))
            return 5;
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

    /* GET may be above PUT when the producer has wrapped the ring.
     * A valid stream must explicitly jump back to PUT before it can finish. */
    if ((get & 3u) || (put & 3u) ||
        get > mapped_length || put > mapped_length)
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
            if (subroutine_active)
                return 5;
            if (target >= mapped_length)
                return 4; /* Invalid target is a protection fault, not CALL_ACTIVE. */
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

/* Report DMA pusher faults through the real PFIFO state/interrupt
 * registers. The standalone pusher validates a complete stream before it
 * dispatches anything, but software must still see hardware-style errors. */
static void raise_dma_pusher_error(int result)
{
    /* submit_dma_pushbuffer_info wraps parser errors as 16+result during
     * validation and 32+result during dispatch. Normalize both paths. */
    int parser_result = result;
    if (parser_result >= 16 && parser_result < 32)
        parser_result -= 16;
    else if (parser_result >= 32 && parser_result < 48)
        parser_result -= 32;

    uint32_t error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION;
    if (parser_result == 5)
        error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
    else if (parser_result == 6)
        error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RETURN;
    else if (parser_result == 8)
        error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD;
    /* Puller validation returns 10+reason. Invalid RAMHT/object handles
     * are NON_CACHE faults; invalid engines/methods/instance addresses
     * remain protection faults. */
    else if (parser_result == 11 || parser_result == 14)
        error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_NON_CACHE;

    /* nv2a_mmio_write takes the global MMIO address, not the PFIFO-local
     * register offset. */
    nv2a_mmio_write(g_gpu, 0x002000u + NV_PFIFO_CACHE1_DMA_STATE,
                    error << 29, 4u);
    g_gpu->pfifo.pending_interrupts |= NV_PFIFO_INTR_0_DMA_PUSHER;
    nv2a_update_irq(g_gpu);
}

static int submit_dma_pushbuffer_info(uint32_t channel,
                                       const XboxNV2A_DMA_INFO* dma,
                                       uint32_t get, uint32_t put,
                                       uint32_t* new_get,
                                       uint32_t* methods_submitted)
{
    if (!dma) return 1;
    if (dma->dma_class != 0x02u && dma->dma_class != 0x3Du)
        return 8;
    if (get > dma->mapped_length || put > dma->mapped_length ||
        (get & 3u) || (put & 3u))
        return 9;

    uint32_t checked_get = get, checked_methods = 0;
    int result = walk_dma_pushbuffer(channel, dma->base_address,
        dma->mapped_length, get, put, &checked_get, &checked_methods, 0);
    if (result != 0) {
        raise_dma_pusher_error(result);
        return 16 + result;
    }

    /* Only dispatch after the complete packet/control-flow stream validates. */
    uint32_t executed_get = get, executed_methods = 0;
    result = walk_dma_pushbuffer(channel, dma->base_address,
        dma->mapped_length, get, put, &executed_get, &executed_methods, 1);
    if (result != 0) {
        raise_dma_pusher_error(result);
        return 32 + result;
    }
    if (new_get) *new_get = executed_get;
    if (methods_submitted) *methods_submitted = executed_methods;
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

    const int result = XboxNV2A_ResolveDMAHandle(channel, handle, &dma);
    if (result != 0) return 2 + result;
    return submit_dma_pushbuffer_info(channel, &dma, get, put,
                                      new_get, methods_submitted);
}

int __cdecl XboxNV2A_SubmitDMAInstance(uint32_t channel, uint32_t instance_offset,
                                       uint32_t get, uint32_t put,
                                       uint32_t* new_get,
                                       uint32_t* methods_submitted)
{
    XboxNV2A_DMA_INFO dma;
    if (new_get) *new_get = get;
    if (methods_submitted) *methods_submitted = 0;
    if (!g_started || !g_gpu || channel > 31u) return 1;

    const int result = resolve_dma_instance(instance_offset, 0u, 0u, &dma);
    if (result != 0) return 2 + result;
    return submit_dma_pushbuffer_info(channel, &dma, get, put,
                                      new_get, methods_submitted);
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
                submit_pgraph_method(0u, subchannel,
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
        submit_pgraph_method(0u, subchannel, pairs[i * 2u], pairs[i * 2u + 1u]);
    return 0;
}

void __cdecl XboxNV2A_Flush(void)
{
    if (g_started) {
        pgraph_d3d11_flush();
        sync_surface_to_vram();
    }
}

int __cdecl XboxNV2A_GetStats(XboxNV2A_STATS* output)
{
    PgraphD3D11Stats stats;

    if (!g_started || !output)
        return 1;

    pgraph_d3d11_get_stats(&stats);
    output->frames = stats.frames;
    output->draw_calls = stats.draw_calls + g_pgraph_custom_draws;
    output->vertices_submitted = stats.vertices_submitted + g_pgraph_custom_vertices;
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
