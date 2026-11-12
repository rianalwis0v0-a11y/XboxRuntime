#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using StartFn = int (__cdecl*)();
using StopFn = void (__cdecl*)();
using ReadFn = uint64_t (__cdecl*)(uint32_t, unsigned int);
using WriteFn = int (__cdecl*)(uint32_t, uint64_t, unsigned int);
using SubmitFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t);
using FlushFn = void (__cdecl*)();
struct Stats {
    uint32_t frames;
    uint32_t draw_calls;
    uint32_t vertices_submitted;
    uint32_t methods_handled;
    uint32_t methods_ignored;
    uint32_t clears;
};
using StatsFn = int (__cdecl*)(Stats*);

using ReadMemoryFn = int (__cdecl*)(uint32_t, void*, uint32_t);
using WriteMemoryFn = int (__cdecl*)(uint32_t, const void*, uint32_t);
using BatchFn = int (__cdecl*)(uint32_t, const uint32_t*, uint32_t);
using PushBufferFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*);
struct DmaInfo {
    uint32_t handle, instance_offset, dma_class, dma_target;
    uint32_t engine, base_address, limit, mapped_length;
};
using LookupRamhtFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*);
using ResolveDmaFn = int (__cdecl*)(uint32_t, uint32_t, DmaInfo*);
using SubmitDmaPushFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
using MapRenderStateFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*, uint32_t*);
using SubmitDmaInstanceFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
using GetDmaContextFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, DmaInfo*);
using ReadVertexArrayFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, void*, uint32_t, uint32_t*, uint32_t*);
struct SurfaceInfo {
    uint32_t width, height, color_format, zeta_format;
    uint32_t color_bytes_per_pixel, zeta_bytes_per_pixel;
    uint32_t color_pitch, zeta_pitch, color_offset, zeta_offset;
};
using DecodeSurfaceFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, SurfaceInfo*);


static FARPROC Need(HMODULE module, const char* name) {
    FARPROC p = GetProcAddress(module, name);
    if (!p) {
        std::cerr << "FAIL: missing nv2a.dll export " << name << "\n";
        ExitProcess(10);
    }
    return p;
}

int main() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!n || n >= path.size()) {
        std::cerr << "FAIL: cannot locate smoke-test executable\n";
        return 1;
    }
    std::wstring dll(path.data(), n);
    const size_t slash = dll.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return 1;
    dll.resize(slash + 1);
    dll += L"nv2a.dll";

    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        std::cerr << "FAIL: LoadLibrary(nv2a.dll) error=" << GetLastError() << "\n";
        return 1;
    }

    auto start = reinterpret_cast<StartFn>(Need(module, "ModuleStart"));
    auto stop = reinterpret_cast<StopFn>(Need(module, "ModuleStop"));
    auto read = reinterpret_cast<ReadFn>(Need(module, "XboxNV2A_ReadRegister"));
    auto write = reinterpret_cast<WriteFn>(Need(module, "XboxNV2A_WriteRegister"));
    auto submit = reinterpret_cast<SubmitFn>(Need(module, "XboxNV2A_SubmitMethod"));
    auto flush = reinterpret_cast<FlushFn>(Need(module, "XboxNV2A_Flush"));
    auto getStats = reinterpret_cast<StatsFn>(Need(module, "XboxNV2A_GetStats"));

    auto readVram = reinterpret_cast<ReadMemoryFn>(Need(module, "XboxNV2A_ReadVRAM"));
    auto writeVram = reinterpret_cast<WriteMemoryFn>(Need(module, "XboxNV2A_WriteVRAM"));
    auto readRamin = reinterpret_cast<ReadMemoryFn>(Need(module, "XboxNV2A_ReadRAMIN"));
    auto writeRamin = reinterpret_cast<WriteMemoryFn>(Need(module, "XboxNV2A_WriteRAMIN"));
    auto submitBatch = reinterpret_cast<BatchFn>(Need(module, "XboxNV2A_SubmitMethods"));
    auto submitPushBuffer = reinterpret_cast<PushBufferFn>(Need(module, "XboxNV2A_SubmitPushBuffer"));
    auto lookupRamht = reinterpret_cast<LookupRamhtFn>(Need(module, "XboxNV2A_LookupRAMHT"));
    auto resolveDma = reinterpret_cast<ResolveDmaFn>(Need(module, "XboxNV2A_ResolveDMAHandle"));
    auto submitDmaPush = reinterpret_cast<SubmitDmaPushFn>(Need(module, "XboxNV2A_SubmitDMAPushBuffer"));
    auto submitDmaInstance = reinterpret_cast<SubmitDmaInstanceFn>(Need(module, "XboxNV2A_SubmitDMAInstance"));
    auto getDmaContext = reinterpret_cast<GetDmaContextFn>(Need(module, "XboxNV2A_GetDMAContext"));
    auto readVertexArray = reinterpret_cast<ReadVertexArrayFn>(Need(module, "XboxNV2A_ReadVertexArray"));
    auto mapRenderState = reinterpret_cast<MapRenderStateFn>(Need(module, "XboxNV2A_MapRenderState"));
    auto decodeSurface = reinterpret_cast<DecodeSurfaceFn>(Need(module, "XboxNV2A_DecodeSurface"));


    uint32_t mappedState = 0, mappedValue = 0;
    if (!mapRenderState(0x033Cu, 0x0203u, &mappedState, &mappedValue) ||
        mappedState != 25u || mappedValue != 4u ||
        !mapRenderState(0x0350u, 0x800Bu, &mappedState, &mappedValue) ||
        mappedState != 171u || mappedValue != 3u ||
        !mapRenderState(0x0370u, 0x1E02u, &mappedState, &mappedValue) ||
        mappedState != 53u || mappedValue != 4u ||
        !mapRenderState(0x029Cu, 0x0801u, &mappedState, &mappedValue) ||
        mappedState != 35u || mappedValue != 2u ||
        !mapRenderState(0x039Cu, 0x0404u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 2u ||
        !mapRenderState(0x039Cu, 0x0405u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 3u ||
        mapRenderState(0x0999u, 0u, &mappedState, &mappedValue)) {
        std::cerr << "FAIL: NV097 fixed-function render-state mapping\\n";
        FreeLibrary(module);
        return 1;
    }

    SurfaceInfo surfaceInfo{};
    const uint32_t surfaceFormat = 0x03u | (0x02u << 4) | (1u << 8);
    const uint32_t surfacePitch = (2560u << 16) | 1280u;
    if (decodeSurface(surfaceFormat, surfacePitch, 0x10000u, 0x200000u,
                      640u << 16, 480u << 16, &surfaceInfo) != 0 ||
        surfaceInfo.width != 640u || surfaceInfo.height != 480u ||
        surfaceInfo.color_format != 5u || surfaceInfo.zeta_format != 0x2Au ||
        surfaceInfo.color_bytes_per_pixel != 2u ||
        surfaceInfo.zeta_bytes_per_pixel != 4u ||
        decodeSurface(surfaceFormat, (2560u << 16) | 1000u,
                      0x10000u, 0x200000u, 640u << 16, 480u << 16,
                      &surfaceInfo) == 0 ||
        decodeSurface(surfaceFormat, surfacePitch,
                      64u * 1024u * 1024u - 100u, 0x200000u,
                      640u << 16, 480u << 16, &surfaceInfo) == 0) {
        std::cerr << "FAIL: NV097 linear surface decode / VRAM bounds\\n";
        FreeLibrary(module);
        return 1;
    }

    if (start() != 0) {
        std::cerr << "FAIL: NV2A module initialization\n";
        return 1;
    }


    const uint32_t pattern[4] = {0x11223344u, 0x55667788u, 0xAABBCCDDu, 0x01020304u};
    uint32_t copy[4]{};
    if (writeVram(0x1000, pattern, sizeof(pattern)) != 0 ||
        readVram(0x1000, copy, sizeof(copy)) != 0 ||
        memcmp(pattern, copy, sizeof(pattern)) != 0) {
        std::cerr << "FAIL: VRAM read/write round trip\\n";
        stop();
        return 1;
    }
    if (writeRamin(0x80, pattern, sizeof(pattern)) != 0 ||
        readRamin(0x80, copy, sizeof(copy)) != 0 ||
        memcmp(pattern, copy, sizeof(pattern)) != 0) {
        std::cerr << "FAIL: RAMIN read/write round trip\\n";
        stop();
        return 1;
    }
    /* Configure a 4 KiB RAMHT and install one NV2A DMA object. */
    const uint32_t dmaHandle = 0x12345678u;
    const uint32_t channel = 2u;
    uint32_t ramhtHash = 0;
    for (uint32_t value = dmaHandle; value; value >>= 9) ramhtHash ^= value & 0x1FFu;
    ramhtHash ^= channel << 5;
    ramhtHash &= 0x1FFu;
    const uint32_t ramhtEntry[2] = {
        dmaHandle, 0x80000000u | (channel << 24) | (1u << 16) | 0x10u
    };
    const uint32_t dmaObject[3] = { 0x00000002u, 0x00000FFFu, 0x00001000u };
    uint32_t ramhtContext = 0;
    DmaInfo dmaInfo{};
    if (write(0x2210u, 0u, 4u) != 0 ||
        writeRamin(ramhtHash * 8u, ramhtEntry, sizeof(ramhtEntry)) != 0 ||
        writeRamin(0x100u, dmaObject, sizeof(dmaObject)) != 0 ||
        lookupRamht(channel, dmaHandle, &ramhtContext) != 0 ||
        ramhtContext != ramhtEntry[1] ||
        resolveDma(channel, dmaHandle, &dmaInfo) != 0 ||
        dmaInfo.instance_offset != 0x100u || dmaInfo.dma_class != 2u ||
        dmaInfo.engine != 1u || dmaInfo.base_address != 0x1000u ||
        dmaInfo.mapped_length != 0x0FFFu ||
        lookupRamht(channel, dmaHandle + 1u, &ramhtContext) == 0 ||
        lookupRamht(channel + 1u, dmaHandle, &ramhtContext) == 0) {
        std::cerr << "FAIL: RAMHT lookup / DMA object resolution\\n";
        stop();
        return 1;
    }

    /* Exercise NV4-style DMA GET/PUT with a real RAMHT-bound PGRAPH object.
     * CALL enters a helper at +24, binds the object, RETURN resumes at +4. */
    const uint32_t dmaCallStream[] = {
        0x0000001Au,                         /* CALL +24 */
        (1u << 18) | 0x1D90u, 0xFF334455u,   /* Main stream: set clear colour */
        (1u << 18) | 0x0180u, dmaHandle,     /* Resolve a RAMHT object method */
        0u,                                  /* alignment/padding before helper */
        (1u << 18), dmaHandle,               /* Subroutine: bind object */
        0x00020000u                          /* RETURN */
    };
    uint32_t dmaGet = 0, dmaMethods = 0;
    const int dmaCallResult = writeVram(0x1000u, dmaCallStream, sizeof(dmaCallStream)) == 0
        ? submitDmaPush(channel, dmaHandle, 0u, 20u, &dmaGet, &dmaMethods) : -1;
    const uint64_t resolvedMethod = read(0x400060u, 4u);
    if (dmaCallResult != 0 || dmaGet != 20u || dmaMethods != 3u || resolvedMethod != 0x100u) {
        std::cerr << "FAIL: NV2A DMA CALL/RETURN result=" << dmaCallResult
                  << " get=" << dmaGet << " methods=" << dmaMethods
                  << " resolved=0x" << std::hex << resolvedMethod << "\\n";
        stop();
        return 1;
    }

    DmaInfo boundVertexContext{};
    if (getDmaContext(channel, 0u, 0x0180u, &boundVertexContext) != 0 ||
        boundVertexContext.handle != dmaHandle ||
        boundVertexContext.instance_offset != 0x100u ||
        boundVertexContext.base_address != 0x1000u) {
        std::cerr << "FAIL: NV097 context-DMA binding was not retained\\n";
        stop();
        return 1;
    }

    /* Bind VERTEX_A and fetch two float3 elements through its DMA object. */
    const uint32_t vertexContextStream[] = { (1u << 18) | 0x019Cu, dmaHandle };
    const float vertexData[6] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    float fetchedVertices[6]{};
    uint32_t vertexStride = 0, vertexElementBytes = 0;
    if (writeVram(0x1000u, vertexContextStream, sizeof(vertexContextStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(vertexContextStream),
                      &dmaGet, &dmaMethods) != 0 ||
        writeVram(0x1300u, vertexData, sizeof(vertexData)) != 0 ||
        submit(0u, 0x1720u, 0x300u) != 0 ||
        submit(0u, 0x1760u, (12u << 8) | (3u << 4) | 2u) != 0 ||
        readVertexArray(channel, 0u, 0u, 0u, 2u, fetchedVertices,
                        sizeof(fetchedVertices), &vertexStride,
                        &vertexElementBytes) != 0 ||
        vertexStride != 12u || vertexElementBytes != 12u ||
        memcmp(vertexData, fetchedVertices, sizeof(vertexData)) != 0 ||
        readVertexArray(channel, 0u, 0u, 0u, 2u, fetchedVertices,
                        12u, &vertexStride, &vertexElementBytes) == 0) {
        std::cerr << "FAIL: NV097 VERTEX_A float3 DMA fetch\\n";
        stop();
        return 1;
    }

    /* Invalid RAMHT handles are rejected before any earlier methods dispatch. */
    const uint32_t badHandleStream[] = {
        (1u << 18), dmaHandle,
        (1u << 18) | 0x1D90u, 0xFFABCDEFu,
        (1u << 18), dmaHandle + 1u
    };
    Stats beforeBadHandle{}, afterBadHandle{};
    if (getStats(&beforeBadHandle) != 0 ||
        writeVram(0x1000u, badHandleStream, sizeof(badHandleStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(badHandleStream),
                      &dmaGet, &dmaMethods) == 0 ||
        dmaMethods != 0 ||
        getStats(&afterBadHandle) != 0 ||
        beforeBadHandle.methods_handled != afterBadHandle.methods_handled) {
        std::cerr << "FAIL: DMA pusher partially dispatched before bad RAMHT handle\\n";
        stop();
        return 1;
    }

    /* A jump may target a command beyond PUT and jump back to PUT. */
    const uint32_t dmaJumpStream[] = {
        0x00000011u,                         /* JUMP +16 */
        0u, 0u, 0u,
        (1u << 18) | 0x1D90u, 0xFF556677u,
        0x00000005u                          /* JUMP +4 (the PUT position) */
    };
    if (writeVram(0x1000u, dmaJumpStream, sizeof(dmaJumpStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, 4u, &dmaGet, &dmaMethods) != 0 ||
        dmaGet != 4u || dmaMethods != 1u) {
        std::cerr << "FAIL: NV2A DMA pusher JUMP execution\\n";
        stop();
        return 1;
    }

    /* PFIFO can resolve CACHE1_DMA_INSTANCE directly, without a RAMHT handle. */
    if (writeVram(0x1000u, dmaCallStream, sizeof(dmaCallStream)) != 0 ||
        submitDmaInstance(channel, 0x100u, 0u, 20u, &dmaGet, &dmaMethods) != 0 ||
        dmaGet != 20u || dmaMethods != 3u) {
        std::cerr << "FAIL: NV2A DMA pusher instance-address path\\n";
        stop();
        return 1;
    }

    /* Writing CACHE1_DMA_PUT in DMA mode must kick the pusher and advance GET. */
    if (writeVram(0x1000u, dmaJumpStream, sizeof(dmaJumpStream)) != 0 ||
        write(0x3204u, channel | 0x100u, 4u) != 0 ||
        write(0x3220u, 1u, 4u) != 0 ||
        write(0x322Cu, 0x10u, 4u) != 0 ||
        write(0x3244u, 0u, 4u) != 0 ||
        write(0x3240u, 4u, 4u) != 0 ||
        read(0x3244u, 4u) != 4u) {
        std::cerr << "FAIL: PFIFO CACHE1 DMA_PUT did not execute/update GET\\n";
        stop();
        return 1;
    }

    if (writeVram(64u * 1024u * 1024u - 2u, pattern, sizeof(pattern)) == 0 ||
        readRamin(1024u * 1024u, copy, 1) == 0) {
        std::cerr << "FAIL: GPU memory bounds were not enforced\\n";
        stop();
        return 1;
    }
    const uint32_t batch[] = {0x01D4, 0xFF223344, 0x17FC, 5, 0x17FC, 0};
    if (submitBatch(0, batch, 3) != 0 || submitBatch(8, batch, 3) == 0) {
        std::cerr << "FAIL: PGRAPH batch validation/dispatch\\n";
        stop();
        return 1;
    }

    /* Exercise real NV4-style pushbuffer packet decoding from VRAM. */
    const uint32_t pushbuffer[] = {
        0x40000000u | (2u << 18) | 0x17FCu, 5u, 0u, /* non-incrementing */
        (1u << 18) | 0x01D4u, 0xFF223344u          /* incrementing */
    };
    uint32_t methodsSubmitted = 0;
    if (writeVram(0x2000, pushbuffer, sizeof(pushbuffer)) != 0 ||
        submitPushBuffer(0x2000, sizeof(pushbuffer), &methodsSubmitted) != 0 ||
        methodsSubmitted != 3) {
        std::cerr << "FAIL: NV2A pushbuffer packet decode/dispatch\n";
        stop();
        return 1;
    }
    const uint32_t malformedPushbuffer[] = { 0x00000001u };
    const uint32_t unsupportedPushbuffer[] = {
        (4u << 29) | (1u << 18) | 0x01D4u, 0xFF000000u
    };
    const uint32_t partiallyInvalidPushbuffer[] = {
        (1u << 18) | 0x01D4u, 0xFF000000u,
        (4u << 29) | (1u << 18) | 0x01D4u, 0xFF000000u
    };
    Stats beforeInvalid{};
    Stats afterInvalid{};
    if (getStats(&beforeInvalid) != 0 ||
        writeVram(0x2100, malformedPushbuffer, sizeof(malformedPushbuffer)) != 0 ||
        submitPushBuffer(0x2100, sizeof(malformedPushbuffer), &methodsSubmitted) == 0 ||
        methodsSubmitted != 0 ||
        writeVram(0x2200, unsupportedPushbuffer, sizeof(unsupportedPushbuffer)) != 0 ||
        submitPushBuffer(0x2200, sizeof(unsupportedPushbuffer), &methodsSubmitted) == 0 ||
        methodsSubmitted != 0 ||
        writeVram(0x2300, partiallyInvalidPushbuffer, sizeof(partiallyInvalidPushbuffer)) != 0 ||
        submitPushBuffer(0x2300, sizeof(partiallyInvalidPushbuffer), &methodsSubmitted) == 0 ||
        methodsSubmitted != 0 ||
        getStats(&afterInvalid) != 0 ||
        beforeInvalid.methods_handled != afterInvalid.methods_handled) {
        std::cerr << "FAIL: malformed NV2A pushbuffer accepted or partially dispatched\\n";
        stop();
        return 1;
    }

    const uint64_t boot = read(0x00000000, 4);
    if (boot != 0x02A000A3u) {
        std::cerr << "FAIL: PMC_BOOT_0 expected 0x02A000A3 got 0x"
                  << std::hex << boot << "\n";
        stop();
        return 1;
    }

    const uint64_t pci = read(0x001800, 4);
    if (pci != 0x02A010DEu) {
        std::cerr << "FAIL: NV2A PCI identity expected 0x02A010DE got 0x"
                  << std::hex << pci << "\n";
        stop();
        return 1;
    }

    if (write(0x000140, 1, 4) != 0 || read(0x000140, 4) != 1) {
        std::cerr << "FAIL: PMC interrupt-enable MMIO write/read\n";
        stop();
        return 1;
    }
    if (write(0x000140, 0, 3) != 2) {
        std::cerr << "FAIL: invalid MMIO access size was not rejected\n";
        stop();
        return 1;
    }

    // Exercise PGRAPH method dispatch without requiring a window or a D3D device.
    if (submit(0, 0x01D4, 0xFF112233) != 0 ||
        submit(0, 0x17FC, 5) != 0 ||
        submit(0, 0x17FC, 0) != 0) {
        std::cerr << "FAIL: PGRAPH method submission\n";
        stop();
        return 1;
    }
    flush();

    Stats stats{};
    if (getStats(&stats) != 0 || stats.methods_handled < 3) {
        std::cerr << "FAIL: PGRAPH statistics did not record submitted methods\n";
        stop();
        return 1;
    }

    stop();
    std::cout << "PASS: NV2A module init, PMC/PBUS MMIO, access validation, "
                 "PGRAPH method dispatch, and statistics\n";
    std::cout << "LIMIT: no D3D device or rendered frame was tested\n";
    FreeLibrary(module);
    return 0;
}
