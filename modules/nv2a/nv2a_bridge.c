#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "nv2a_bridge.h"
#include "nv2a_state.h"
#include "nv2a_pgraph_d3d11.h"

#define XBOX_VRAM_SIZE  (64u * 1024u * 1024u)
#define XBOX_RAMIN_SIZE (1u * 1024u * 1024u)
#define XBOX_GPU_MEMORY_SIZE (XBOX_VRAM_SIZE + XBOX_RAMIN_SIZE)

static uint8_t* g_gpu_memory;
static NV2AState* g_gpu;
static int g_started;

int __cdecl ModuleStart(void)
{
    if (g_started)
        return 0;

    // Keep the Xbox's 64 MiB VRAM + 1 MiB RAMIN model. This is host-backed
    // memory, not a limit on the PC's physical RAM.
    g_gpu_memory = (uint8_t*)VirtualAlloc(
        NULL,
        XBOX_GPU_MEMORY_SIZE,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
    );

    if (!g_gpu_memory) {
        OutputDebugStringW(L"[XboxRuntime:NV2A] VRAM/RAMIN allocation failed.\n");
        return 1;
    }

    g_gpu = nv2a_init_standalone(
        g_gpu_memory,
        XBOX_VRAM_SIZE,
        g_gpu_memory + XBOX_VRAM_SIZE,
        XBOX_RAMIN_SIZE
    );

    if (!g_gpu) {
        VirtualFree(g_gpu_memory, 0, MEM_RELEASE);
        g_gpu_memory = NULL;
        OutputDebugStringW(L"[XboxRuntime:NV2A] nv2a_init_standalone failed.\n");
        return 2;
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
