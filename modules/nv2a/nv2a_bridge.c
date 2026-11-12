#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "nv2a_bridge.h"
#include "nv2a_state.h"
#include "nv2a_pgraph_d3d11.h"

#define XBOX_VRAM_SIZE  (64u * 1024u * 1024u)
#define XBOX_RAMIN_SIZE (1u * 1024u * 1024u)
#define XBOX_GPU_MEMORY_SIZE (XBOX_VRAM_SIZE + XBOX_RAMIN_SIZE)

static uint8_t* g_vram;
static uint8_t* g_ramin;

static uint8_t* g_gpu_memory;
static NV2AState* g_gpu;
static int g_started;

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

    pgraph_method(g_gpu, subchannel, method, parameter);
    return 0;
}


static int memory_range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return offset <= capacity && length <= capacity - offset;
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
        uint32_t command_type = header & 0x60000000u;
        if (command_type != 0u && command_type != 0x40000000u)
            return 2; /* Reject jump/call/return/long/unknown commands. */

        uint32_t count = (header >> 18) & 0x7FFu;
        uint32_t subchannel = (header >> 13) & 7u;
        uint32_t method = header & 0x1FFCu;
        if (!count || method >= 0x2000u * 4u ||
            count > words - cursor || count > 1048576u - total)
            return 3;

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t parameter;
            memcpy(&parameter, g_vram + offset + (cursor + i) * 4u, sizeof(parameter));
            if (dispatch)
                pgraph_method(g_gpu, subchannel,
                    command_type == 0x40000000u ? method : method + i * 4u,
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
        pgraph_method(g_gpu, subchannel, pairs[i * 2u], pairs[i * 2u + 1u]);
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
    output->methods_handled = stats.methods_handled;
    output->methods_ignored = stats.methods_ignored;
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
