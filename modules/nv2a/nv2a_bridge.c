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

static uint32_t g_bound_engine[32][8];
static uint32_t g_ref_counter[32];

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
        pgraph_method(g_gpu, subchannel, 0u, instance);
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

    pgraph_method(g_gpu, subchannel, method, parameter);
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
            if (subroutine_active || target >= mapped_length || target >= put)
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
            if (!remaining || remaining > (put - get) / 4u ||
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
                pgraph_method(g_gpu, subchannel,
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
