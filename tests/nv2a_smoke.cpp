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
struct TextureInfo { uint32_t texture_offset,format,width,height,level_count,source_pitch,context_method,enabled,linear; };
struct DmaInfo {
    uint32_t handle, instance_offset, dma_class, dma_target;
    uint32_t engine, base_address, limit, mapped_length;
};
using LookupRamhtFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*);
using ResolveDmaFn = int (__cdecl*)(uint32_t, uint32_t, DmaInfo*);
using SubmitDmaPushFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
using MapRenderStateFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t*, uint32_t*);
using MapTextureFilterFn = int (__cdecl*)(uint32_t, uint32_t*, uint32_t*, uint32_t*);
using SubmitDmaInstanceFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
using GetDmaContextFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, DmaInfo*);
using ReadVertexArrayFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, void*, uint32_t, uint32_t*, uint32_t*);
struct HostVertex { float x, y, z, nx, ny, nz; uint32_t diffuse, specular; float u, v, u1, v1, u2, v2, u3, v3; };
using BuildIndexedVertexStreamFn = int (__cdecl*)(uint32_t,uint32_t,const uint32_t*,uint32_t,HostVertex*,uint32_t,uint32_t*,uint32_t*);
using BuildVertexStreamFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t,
    HostVertex*, uint32_t, uint32_t*, uint32_t*);
struct SurfaceInfo {
    uint32_t width, height, color_format, zeta_format;
    uint32_t color_bytes_per_pixel, zeta_bytes_per_pixel;
    uint32_t color_pitch, zeta_pitch, color_offset, zeta_offset;
};
using DecodeTextureFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,
    const void*,uint32_t,void*,uint32_t,uint32_t*);
using DecodeSwizzledTextureFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,const void*,uint32_t,void*,uint32_t,uint32_t*);
using DecodeMipChainFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,const void*,uint32_t,void*,uint32_t,uint32_t*,uint32_t*);
using DecodeMipChainDmaFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t,uint32_t*,uint32_t*);
using DecodeTextureDmaFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,void*,uint32_t,uint32_t*);
using GetTextureInfoFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,TextureInfo*);
using DecodeTextureStageFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,void*,uint32_t,uint32_t*,uint32_t*);
using DecodeSurfaceFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, SurfaceInfo*);
using ValidateVertexProgramFn = int (__cdecl*)(const uint32_t*, uint32_t, void*);


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
    auto buildVertexStream = reinterpret_cast<BuildVertexStreamFn>(Need(module, "XboxNV2A_BuildVertexStream"));
    auto buildIndexedVertexStream = reinterpret_cast<BuildIndexedVertexStreamFn>(Need(module, "XboxNV2A_BuildIndexedVertexStream"));
    auto mapRenderState = reinterpret_cast<MapRenderStateFn>(Need(module, "XboxNV2A_MapRenderState"));
    auto mapTextureFilter = reinterpret_cast<MapTextureFilterFn>(Need(module, "XboxNV2A_MapTextureFilter"));
    auto decodeTexture = reinterpret_cast<DecodeTextureFn>(Need(module, "XboxNV2A_DecodeTextureRGBA8"));
    auto decodeMipChain = reinterpret_cast<DecodeMipChainFn>(Need(module, "XboxNV2A_DecodeTextureMipChainRGBA8"));
    auto decodeMipChainFromDma = reinterpret_cast<DecodeMipChainDmaFn>(Need(module, "XboxNV2A_DecodeTextureMipChainFromDMA"));
    auto decodeTextureFromDma = reinterpret_cast<DecodeTextureDmaFn>(Need(module, "XboxNV2A_DecodeTextureFromDMA"));
    auto getTextureInfo = reinterpret_cast<GetTextureInfoFn>(Need(module, "XboxNV2A_GetTextureInfo"));
    auto decodeTextureStage = reinterpret_cast<DecodeTextureStageFn>(Need(module, "XboxNV2A_DecodeTextureStageRGBA8"));
    auto decodeSwizzledTexture = reinterpret_cast<DecodeSwizzledTextureFn>(Need(module, "XboxNV2A_DecodeSwizzledTextureRGBA8"));
    auto decodeSurface = reinterpret_cast<DecodeSurfaceFn>(Need(module, "XboxNV2A_DecodeSurface"));
    auto validateVertexProgram = reinterpret_cast<ValidateVertexProgramFn>(Need(module, "XboxNV2A_ValidateVertexProgram"));


    {
        struct VertexProgramInfo {
            uint32_t instruction_count;
            uint16_t input_mask, output_mask;
            uint64_t constant_mask[3];
            uint32_t uses_relative_constants;
        };
        VertexProgramInfo info{};
        uint32_t words[4]{};
        words[3] = 1u; /* A single NOP instruction with FINAL set. */
        if (validateVertexProgram(words, 1, &info) != 0 ||
            info.instruction_count != 1) {
            std::cerr << "FAIL: minimal NV2A vertex program validation\\n";
            FreeLibrary(module); return 1;
        }

        /* MAC MOV from c[17], with instruction-wide relative addressing enabled. */
        std::memset(words, 0, sizeof(words));
        words[1] = (1u << 21) | (17u << 13);
        words[2] = (3u << 26);
        words[3] = (1u << 1) | 1u;
        if (validateVertexProgram(words, 1, &info) != 0 ||
            info.uses_relative_constants != 1 ||
            !(info.constant_mask[0] & (UINT64_C(1) << 17))) {
            std::cerr << "FAIL: NV2A relative constant source metadata\\n";
            FreeLibrary(module); return 1;
        }

        /* ADD is A+C, not A+B. An unused B mux must not pollute metadata. */
        std::memset(words, 0, sizeof(words));
        words[1] = (3u << 21) | (5u << 9);
        words[2] = (2u << 26);
        words[3] = (2u << 28) | 1u; /* C mux=input */
        if (validateVertexProgram(words, 1, &info) != 0 ||
            info.input_mask != (1u << 5) ||
            info.constant_mask[0] || info.constant_mask[1] ||
            info.constant_mask[2]) {
            std::cerr << "FAIL: NV2A ADD A+C source decoding\\n";
            FreeLibrary(module); return 1;
        }

        /* ADD's C operand can independently select a physical constant. */
        std::memset(words, 0, sizeof(words));
        words[1] = (3u << 21) | (7u << 13) | (1u << 9);
        words[2] = (2u << 26);
        words[3] = (3u << 28) | 1u; /* C mux=constant */
        if (validateVertexProgram(words, 1, &info) != 0 ||
            !(info.input_mask & (1u << 1)) ||
            !(info.constant_mask[0] & (UINT64_C(1) << 7))) {
            std::cerr << "FAIL: NV2A ADD constant-C metadata\\n";
            FreeLibrary(module); return 1;
        }

        /* ARL reads only A; ILU LIT reads only C. */
        std::memset(words, 0, sizeof(words));
        words[1] = (13u << 21) | (1u << 9);
        words[2] = (2u << 26);
        words[3] = 1u;
        if (validateVertexProgram(words, 1, &info) != 0 ||
            !(info.input_mask & (1u << 1))) {
            std::cerr << "FAIL: NV2A ARL opcode validation\\n";
            FreeLibrary(module); return 1;
        }
        std::memset(words, 0, sizeof(words));
        words[1] = (7u << 25) | (3u << 13);
        words[3] = (3u << 28) | 1u; /* C mux=constant */
        if (validateVertexProgram(words, 1, &info) != 0 ||
            !(info.constant_mask[0] & (UINT64_C(1) << 3))) {
            std::cerr << "FAIL: NV2A LIT constant-C validation\\n";
            FreeLibrary(module); return 1;
        }

        /* BFC0/BFC1 are valid outputs; slots 1 and 2 remain reserved. */
        std::memset(words, 0, sizeof(words));
        words[3] = (1u << 11) | (1u << 12) | (7u << 3) | 1u;
        if (validateVertexProgram(words, 1, &info) != 0 ||
            !(info.output_mask & (1u << 7))) {
            std::cerr << "FAIL: NV2A BFC0 output register validation\\n";
            FreeLibrary(module); return 1;
        }
        words[3] = (1u << 11) | (1u << 12) | (1u << 3) | 1u;
        if (validateVertexProgram(words, 1, &info) != 8) {
            std::cerr << "FAIL: reserved NV2A output register accepted\\n";
            FreeLibrary(module); return 1;
        }
    }

    uint32_t mappedState = 0, mappedValue = 0;
    if (!mapRenderState(0x0300u, 1u, &mappedState, &mappedValue) ||
        mappedState != 15u || mappedValue != 1u ||
        !mapRenderState(0x0308u, 0u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 1u ||
        !mapRenderState(0x0308u, 1u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 3u ||
        !mapRenderState(0x030Cu, 1u, &mappedState, &mappedValue) ||
        mappedState != 7u || mappedValue != 1u ||
        !mapRenderState(0x033Cu, 0x0203u, &mappedState, &mappedValue) ||
        mappedState != 25u || mappedValue != 4u ||
        !mapRenderState(0x0350u, 0x800Bu, &mappedState, &mappedValue) ||
        mappedState != 171u || mappedValue != 3u ||
        !mapRenderState(0x0370u, 0x1E02u, &mappedState, &mappedValue) ||
        mappedState != 53u || mappedValue != 4u ||
        !mapRenderState(0x029Cu, 0x0801u, &mappedState, &mappedValue) ||
        mappedState != 35u || mappedValue != 2u ||
        !mapRenderState(0x02A4u, 1u, &mappedState, &mappedValue) ||
        mappedState != 28u || mappedValue != 1u ||
        !mapRenderState(0x02A8u, 0xAA112233u, &mappedState, &mappedValue) ||
        mappedState != 34u || mappedValue != 0xAA332211u ||
        !mapRenderState(0x039Cu, 0x0404u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 2u ||
        !mapRenderState(0x039Cu, 0x0405u, &mappedState, &mappedValue) ||
        mappedState != 22u || mappedValue != 3u ||
        !mapRenderState(0x0304u, 1u, &mappedState, &mappedValue) ||
        mappedState != 27u || mappedValue != 1u ||
        !mapRenderState(0x0344u, 0x0302u, &mappedState, &mappedValue) ||
        mappedState != 19u || mappedValue != 5u ||
        !mapRenderState(0x0348u, 0x0307u, &mappedState, &mappedValue) ||
        mappedState != 20u || mappedValue != 10u ||
        mapRenderState(0x034Cu, 0x80402010u, &mappedState, &mappedValue) ||
        mapRenderState(0x0344u, 0x7777u, &mappedState, &mappedValue) ||
        mapRenderState(0x0999u, 0u, &mappedState, &mappedValue)) {
        std::cerr << "FAIL: NV097 fixed-function render-state mapping\\n";
        FreeLibrary(module);
        return 1;
    }

    {
        uint32_t minFilter=0, magFilter=0, mipFilter=0;
        const uint32_t cases[] = {
            (1u << 16) | (1u << 24), /* point / point / no mip */
            (2u << 16) | (2u << 24), /* linear / linear / no mip */
            (3u << 16) | (2u << 24), /* point / linear / point mip */
            (4u << 16) | (1u << 24), /* linear / point / point mip */
            (5u << 16) | (4u << 24), /* point / linear / linear mip */
            (6u << 16) | (2u << 24), /* linear / linear / linear mip */
            (7u << 16) | (4u << 24)  /* convolution fallback */
        };
        const uint32_t expectedMin[] = {1,2,1,2,1,2,2};
        const uint32_t expectedMag[] = {1,2,2,1,2,2,2};
        const uint32_t expectedMip[] = {0,0,1,1,2,2,0};
        for (uint32_t i=0; i<7; ++i) {
            if (!mapTextureFilter(cases[i], &minFilter, &magFilter, &mipFilter) ||
                minFilter!=expectedMin[i] || magFilter!=expectedMag[i] ||
                mipFilter!=expectedMip[i]) {
                std::cerr << "FAIL: NV097 min/mag/mip filter mapping case " << i << "\\n";
                FreeLibrary(module); return 1;
            }
        }
        if (mapTextureFilter(0u, &minFilter, &magFilter, &mipFilter) ||
            mapTextureFilter((1u << 16) | (3u << 24), &minFilter, &magFilter, &mipFilter) ||
            mapTextureFilter((1u << 16) | (1u << 24), nullptr, &magFilter, &mipFilter)) {
            std::cerr << "FAIL: invalid NV097 texture filter accepted\\n";
            FreeLibrary(module); return 1;
        }
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

    {
        const uint8_t argb[8] = { 0x33,0x22,0x11,0xFF, 0x66,0x55,0x44,0x80 };
        uint8_t rgba[8]{}; uint32_t pitch=0;
        if (decodeTexture(0x1200u,1,1,0,argb,4,rgba,sizeof(rgba),&pitch)!=0 ||
            rgba[0]!=0x11 || rgba[1]!=0x22 || rgba[2]!=0x33 || rgba[3]!=0xFF ||
            decodeTexture(0x1300u,1,1,0,argb,1,rgba,sizeof(rgba),&pitch)!=0 ||
            rgba[0]!=0x33 || rgba[1]!=0x33 || rgba[2]!=0x33 || rgba[3]!=0xFF) {
            std::cerr << "FAIL: linear-image texture formats\\n"; return 1;
        }
                if (decodeTexture(0x0600u,2,1,0,argb,sizeof(argb),rgba,sizeof(rgba),&pitch)!=0 ||
            pitch!=8 || rgba[0]!=0x11 || rgba[1]!=0x22 || rgba[2]!=0x33 || rgba[3]!=0xFF ||
            rgba[4]!=0x44 || rgba[5]!=0x55 || rgba[6]!=0x66 || rgba[7]!=0x80) {
            std::cerr << "FAIL: A8R8G8B8 texture decode\n"; return 1;
        }
        const uint8_t dxt1[8] = { 0x00,0xF8,0xE0,0x07,0,0,0,0 };
        uint8_t block[4*4*4]{}; 
        if (decodeTexture(0x0C00u,4,4,0,dxt1,sizeof(dxt1),block,sizeof(block),&pitch)!=0 ||
            pitch!=16 || block[0]!=255 || block[1]!=0 || block[2]!=0 || block[3]!=255) {
            std::cerr << "FAIL: DXT1 texture decode\n"; return 1;
        }
        const uint8_t dxt3[16] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0x00,0xF8,0x00,0xF8,0,0,0,0
        };
        const uint8_t dxt5[16] = {
            0xFF,0x00,0,0,0,0,0,0,
            0xE0,0x07,0xE0,0x07,0,0,0,0
        };
        if (decodeTexture(0x0E00u,4,4,0,dxt3,sizeof(dxt3),block,sizeof(block),&pitch)!=0 ||
            block[0]!=255 || block[1]!=0 || block[2]!=0 || block[3]!=255 ||
            decodeTexture(0x0F00u,4,4,0,dxt5,sizeof(dxt5),block,sizeof(block),&pitch)!=0 ||
            block[0]!=0 || block[1]!=255 || block[2]!=0 || block[3]!=255) {
            std::cerr << "FAIL: DXT3/DXT5 texture decode\\n"; return 1;
        }
        if (decodeTexture(0x0600u,2,1,0,argb,sizeof(argb),rgba,4,&pitch)==0 ||
            decodeTexture(0x0600u,2,1,0,argb,4,rgba,sizeof(rgba),&pitch)==0 ||
            decodeTexture(0x9900u,2,1,0,argb,sizeof(argb),rgba,sizeof(rgba),&pitch)==0) {
            std::cerr << "FAIL: texture decode bounds/format validation\n"; return 1;
        }
    }


    {
        /* 2x2 Morton order is (0,0), (1,0), (0,1), (1,1). */
        const uint8_t swizzled[16] = {
            0x03,0x02,0x01,0xFF, 0x06,0x05,0x04,0xFF,
            0x09,0x08,0x07,0xFF, 0x0C,0x0B,0x0A,0xFF
        };
        uint8_t linear[16]{};
        uint32_t pitch = 0;
        if (decodeSwizzledTexture(0x0600u, 2, 2, swizzled, sizeof(swizzled),
                linear, sizeof(linear), &pitch) != 0 ||
            pitch != 8u ||
            linear[0] != 1u || linear[1] != 2u || linear[2] != 3u ||
            linear[4] != 4u || linear[5] != 5u || linear[6] != 6u ||
            linear[8] != 7u || linear[9] != 8u || linear[10] != 9u ||
            linear[12] != 10u || linear[13] != 11u || linear[14] != 12u ||
            decodeSwizzledTexture(0x0600u, 3, 2, swizzled, sizeof(swizzled),
                linear, sizeof(linear), &pitch) == 0 ||
            decodeSwizzledTexture(0x0C00u, 2, 2, swizzled, sizeof(swizzled),
                linear, sizeof(linear), &pitch) == 0) {
            std::cerr << "FAIL: swizzled texture decode / validation\\n";
            return 1;
        }
    }


    {
        const uint8_t linearMipSource[4] = {0x33,0x22,0x11,0xFF};
        uint8_t linearMipOutput[4]{};
        uint32_t linearMipOffset[1]{}, linearMipSize[1]{};
        if (decodeMipChain(0x1200u,1,1,1,linearMipSource,sizeof(linearMipSource),
                linearMipOutput,sizeof(linearMipOutput),linearMipOffset,linearMipSize)!=0 ||
            linearMipOffset[0]!=0u || linearMipSize[0]!=4u ||
            linearMipOutput[0]!=0x11 || linearMipOutput[3]!=0xFF) {
            std::cerr << "FAIL: linear-image mip-chain decode\\n"; return 1;
        }

        const uint8_t mipSource[20] = {
            0x03,0x02,0x01,0xFF, 0x06,0x05,0x04,0xFF,
            0x09,0x08,0x07,0xFF, 0x0C,0x0B,0x0A,0xFF,
            0x0F,0x0E,0x0D,0xFF
        };
        uint8_t mipOutput[20]{};
        uint32_t offsets[2]{}, sizes[2]{};
        if (decodeMipChain(0x0600u, 2, 2, 2, mipSource, sizeof(mipSource),
                mipOutput, sizeof(mipOutput), offsets, sizes) != 0 ||
            offsets[0] != 0u || sizes[0] != 16u ||
            offsets[1] != 16u || sizes[1] != 4u ||
            mipOutput[0] != 1u || mipOutput[4] != 4u ||
            mipOutput[8] != 7u || mipOutput[16] != 13u ||
            decodeMipChain(0x0600u, 2, 2, 2, mipSource, 19u,
                mipOutput, sizeof(mipOutput), offsets, sizes) == 0 ||
            decodeMipChain(0x0600u, 2, 2, 2, mipSource, sizeof(mipSource),
                mipOutput, 19u, offsets, sizes) == 0 ||
            decodeMipChain(0x0600u, 2, 2, 0, mipSource, sizeof(mipSource),
                mipOutput, sizeof(mipOutput), offsets, sizes) == 0) {
            std::cerr << "FAIL: texture mip-chain decode / bounds\\n";
            return 1;
        }
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


    const uint32_t vertexIndices[3] = {1u, 0u, 1u};
    HostVertex indexedVertices[3]{};
    uint32_t indexedStride = 0, indexedFvf = 0;
    if (buildIndexedVertexStream(channel, 0u, vertexIndices, 3u,
            indexedVertices, sizeof(indexedVertices), &indexedStride, &indexedFvf) != 0 ||
        indexedStride != sizeof(HostVertex) || indexedFvf != 0x4D2u ||
        indexedVertices[0].x != 4.0f || indexedVertices[1].x != 1.0f ||
        indexedVertices[2].x != 4.0f ||
        buildIndexedVertexStream(channel, 0u, nullptr, 3u,
            indexedVertices, sizeof(indexedVertices), &indexedStride, &indexedFvf) == 0 ||
        buildIndexedVertexStream(channel, 0u, vertexIndices, 3u,
            indexedVertices, sizeof(HostVertex), &indexedStride, &indexedFvf) == 0) {
        std::cerr << "FAIL: indexed vertex fetch/expansion\\n";
        stop();
        return 1;
    }

    const uint32_t vertexColors[2] = { 0xFF112233u, 0xFF445566u };
    const uint32_t vertexSpecular[2] = { 0x00112233u, 0x00445566u };
    const float vertexNormals[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
    const float vertexTexcoords[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    const float vertexTexcoords1[4] = { 0.125f, 0.25f, 0.625f, 0.875f };
    const float vertexTexcoords2[4] = { 0.2f, 0.3f, 0.4f, 0.5f };
    const float vertexTexcoords3[4] = { 0.6f, 0.7f, 0.8f, 0.9f };
    const uint32_t vertexContextBStream[] = { (1u << 18) | 0x01A0u, dmaHandle };
    HostVertex hostVertices[2]{};
    uint32_t hostVertexStride = 0, hostFvf = 0;
    if (writeVram(0x1400u, vertexColors, sizeof(vertexColors)) != 0 ||
        writeVram(0x1500u, vertexTexcoords, sizeof(vertexTexcoords)) != 0 ||
        writeVram(0x1600u, vertexNormals, sizeof(vertexNormals)) != 0 ||
        writeVram(0x1700u, vertexSpecular, sizeof(vertexSpecular)) != 0 ||
        writeVram(0x1800u, vertexTexcoords1, sizeof(vertexTexcoords1)) != 0 ||
        writeVram(0x1900u, vertexTexcoords2, sizeof(vertexTexcoords2)) != 0 ||
        writeVram(0x1A00u, vertexTexcoords3, sizeof(vertexTexcoords3)) != 0 ||
        writeVram(0x1000u, vertexContextBStream, sizeof(vertexContextBStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(vertexContextBStream),
                      &dmaGet, &dmaMethods) != 0 ||
        submit(0u, 0x1728u, 0x600u) != 0 ||
        submit(0u, 0x1768u, (12u << 8) | (3u << 4) | 2u) != 0 ||
        submit(0u, 0x1730u, 0x700u) != 0 ||
        submit(0u, 0x1770u, (4u << 8) | (4u << 4)) != 0 ||
        submit(0u, 0x172Cu, 0x400u) != 0 ||
        submit(0u, 0x176Cu, (4u << 8) | (4u << 4)) != 0 ||
        submit(0u, 0x173Cu, 0x500u) != 0 ||
        submit(0u, 0x177Cu, (8u << 8) | (2u << 4) | 2u) != 0 ||
        submit(0u, 0x1740u, 0x800u) != 0 ||
        submit(0u, 0x1780u, (8u << 8) | (2u << 4) | 2u) != 0 ||
        submit(0u, 0x1744u, 0x900u) != 0 ||
        submit(0u, 0x1784u, (8u << 8) | (2u << 4) | 2u) != 0 ||
        submit(0u, 0x1748u, 0xA00u) != 0 ||
        submit(0u, 0x1788u, (8u << 8) | (2u << 4) | 2u) != 0 ||
        buildVertexStream(channel, 0u, 0u, 2u, hostVertices,
                          sizeof(hostVertices), &hostVertexStride, &hostFvf) != 0 ||
        hostVertexStride != 64u || hostFvf != 0x4D2u ||
        hostVertices[0].nx != 0.0f || hostVertices[0].ny != 0.0f || hostVertices[0].nz != 1.0f ||
        hostVertices[1].nx != 0.0f || hostVertices[1].ny != 1.0f || hostVertices[1].nz != 0.0f ||
        hostVertices[0].diffuse != vertexColors[0] ||
        hostVertices[1].diffuse != vertexColors[1] ||
        hostVertices[0].specular != vertexSpecular[0] ||
        hostVertices[1].specular != vertexSpecular[1] ||
        hostVertices[0].u != 0.25f || hostVertices[0].v != 0.5f ||
        hostVertices[1].u != 0.75f || hostVertices[1].v != 1.0f ||
        hostVertices[0].u1 != 0.125f || hostVertices[0].v1 != 0.25f ||
        hostVertices[1].u1 != 0.625f || hostVertices[1].v1 != 0.875f ||
        hostVertices[0].u2 != 0.2f || hostVertices[0].v2 != 0.3f ||
        hostVertices[1].u2 != 0.4f || hostVertices[1].v2 != 0.5f ||
        hostVertices[0].u3 != 0.6f || hostVertices[0].v3 != 0.7f ||
        hostVertices[1].u3 != 0.8f || hostVertices[1].v3 != 0.9f ||
        buildVertexStream(channel, 0u, 0u, 2u, hostVertices,
                          sizeof(HostVertex), &hostVertexStride, &hostFvf) == 0) {
        std::cerr << "FAIL: NV2A position/color/TEX0-TEX3 host vertex stream build\\n";
        stop();
        return 1;
    }

    /* Indexed expansion must handle batches larger than the old 255-vertex cap. */
    {
        std::vector<uint32_t> largeIndices(300u);
        std::vector<HostVertex> largeVertices(largeIndices.size());
        for (size_t i = 0; i < largeIndices.size(); ++i)
            largeIndices[i] = (uint32_t)(i & 1u);
        if (buildIndexedVertexStream(channel, 0u, largeIndices.data(),
                (uint32_t)largeIndices.size(), largeVertices.data(),
                (uint32_t)(largeVertices.size() * sizeof(HostVertex)),
                &indexedStride, &indexedFvf) != 0 ||
            indexedStride != sizeof(HostVertex) || indexedFvf != 0x4D2u ||
            largeVertices[0].x != hostVertices[0].x ||
            largeVertices[1].x != hostVertices[1].x ||
            largeVertices[299].x != hostVertices[1].x) {
            std::cerr << "FAIL: indexed vertex expansion above 255 vertices\\n";
            stop();
            return 1;
        }
        std::vector<uint32_t> tooManyIndices(1025u, 0u);
        std::vector<HostVertex> tooManyVertices(tooManyIndices.size());
        if (buildIndexedVertexStream(channel, 0u, tooManyIndices.data(),
                (uint32_t)tooManyIndices.size(), tooManyVertices.data(),
                (uint32_t)(tooManyVertices.size() * sizeof(HostVertex)),
                &indexedStride, &indexedFvf) == 0) {
            std::cerr << "FAIL: indexed vertex expansion exceeded its 1024-vertex bound\\n";
            stop();
            return 1;
        }
    }

    /* UB_OGL arrays are RGBA bytes; the D3D8 FVF requires D3DCOLOR ARGB. */
    const uint32_t oglDiffuse[2] = {0xFF332211u,0xFF665544u};
    const uint32_t oglSpecular[2] = {0x00332211u,0x00665544u};
    if (writeVram(0x1400u,oglDiffuse,sizeof(oglDiffuse))!=0||
        writeVram(0x1700u,oglSpecular,sizeof(oglSpecular))!=0||
        submit(0u,0x176Cu,(4u<<8)|(4u<<4)|4u)!=0||
        submit(0u,0x1770u,(4u<<8)|(4u<<4)|4u)!=0||
        buildVertexStream(channel,0u,0u,2u,hostVertices,sizeof(hostVertices),
            &hostVertexStride,&hostFvf)!=0||
        hostVertices[0].diffuse!=0xFF112233u||
        hostVertices[1].diffuse!=0xFF445566u||
        hostVertices[0].specular!=0x00112233u||
        hostVertices[1].specular!=0x00445566u){
        std::cerr<<"FAIL: UB_OGL vertex color channel conversion\\n";
        stop();return 1;
    }

    /* S1 normals normalize; S32K texture coordinates preserve signed scale. */
    const int16_t signedNormals[6]={32767,0,-32768,0,-16384,16384};
    const int16_t signedUvs[4]={-12,34,56,-78};
    if(writeVram(0x1B00u,signedNormals,sizeof(signedNormals))!=0||
       writeVram(0x1C00u,signedUvs,sizeof(signedUvs))!=0||
       submit(0u,0x1728u,0xB00u)!=0||
       submit(0u,0x1768u,(12u<<8)|(3u<<4)|1u)!=0||
       submit(0u,0x173Cu,0xC00u)!=0||
       submit(0u,0x177Cu,(4u<<8)|(2u<<4)|5u)!=0||
       buildVertexStream(channel,0u,0u,2u,hostVertices,sizeof(hostVertices),
          &hostVertexStride,&hostFvf)!=0||
       hostVertices[0].nx!=1.0f||hostVertices[0].ny!=0.0f||
       hostVertices[0].nz!=-1.0f||hostVertices[1].nx < -0.501f||
       hostVertices[0].u!=-12.0f||hostVertices[0].v!=34.0f||
       hostVertices[1].u!=56.0f||hostVertices[1].v!=-78.0f){
        std::cerr<<"FAIL: S1/S32K NV097 vertex attribute conversion\\n";
        stop();return 1;
    }

    /* CMP packs signed 11/11/10 normal components in one word. */
    const uint32_t compressedNormals[2]={
        (0x1FFu<<22)|(0x400u<<11)|0x3FFu, 0u
    };
    if(writeVram(0x1D00u,compressedNormals,sizeof(compressedNormals))!=0||
       submit(0u,0x1728u,0xD00u)!=0||
       submit(0u,0x1768u,(4u<<8)|(3u<<4)|6u)!=0||
       buildVertexStream(channel,0u,0u,2u,hostVertices,sizeof(hostVertices),
          &hostVertexStride,&hostFvf)!=0||
       hostVertices[0].nx!=1.0f||hostVertices[0].ny > -0.99f||
       hostVertices[0].ny < -1.0f||hostVertices[0].nz > 1.0f||
       hostVertices[0].nz < 0.99f){
        std::cerr<<"FAIL: CMP compressed NV097 normal decode\\n";
        stop();return 1;
    }

    /* Invalid RAMHT handles are rejected before any earlier methods dispatch. */
    const uint32_t badHandleStream[] = {
        (1u << 18), dmaHandle,
        (1u << 18) | 0x1D90u, 0xFFABCDEFu,
        (1u << 18), dmaHandle + 1u
    };
    Stats beforeBadHandle{}, afterBadHandle{};
    const int badStatsBefore = getStats(&beforeBadHandle);
    const int badVramWrite = writeVram(0x1000u, badHandleStream, sizeof(badHandleStream));
    const int badDmaResult = submitDmaPush(channel, dmaHandle, 0u,
        sizeof(badHandleStream), &dmaGet, &dmaMethods);
    const int badStatsAfter = getStats(&afterBadHandle);
    const uint32_t badInterrupt = (uint32_t)read(0x2100u, 4u);
    const uint32_t badDmaState = (uint32_t)read(0x3228u, 4u);
    const int badInterruptClear = write(0x2100u, 0x1000u, 4u);
    const uint32_t clearedInterrupt = (uint32_t)read(0x2100u, 4u);
    if (badStatsBefore != 0 || badVramWrite != 0 || badDmaResult == 0 ||
        dmaMethods != 0 || badStatsAfter != 0 ||
        beforeBadHandle.methods_handled != afterBadHandle.methods_handled ||
        (badInterrupt & 0x1000u) == 0u || (badDmaState >> 29) != 2u ||
        badInterruptClear != 0 || (clearedInterrupt & 0x1000u) != 0u) {
        std::cerr << "FAIL: DMA error reporting: submit=" << badDmaResult
                  << " methods=" << dmaMethods
                  << " intr=0x" << std::hex << badInterrupt
                  << " state=0x" << badDmaState
                  << " clear=" << std::dec << badInterruptClear
                  << " after_clear=0x" << std::hex << clearedInterrupt << "\\n";
        stop();
        return 1;
    }

    /* PFIFO reports the NV1A single-level CALL/RETURN faults in DMA_STATE. */
    const uint32_t nestedCallStream[] = {
        0x00000012u, 0u, 0u, 0u, 0x0000001Eu
    };
    const uint32_t invalidCallTargetStream[] = { 0xFFFFFFFEu };
    const uint32_t inactiveReturnStream[] = { 0x00020000u };
    if (writeVram(0x1000u, nestedCallStream, sizeof(nestedCallStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(nestedCallStream),
                      &dmaGet, &dmaMethods) == 0 ||
        (read(0x2100u, 4u) & 0x1000u) == 0u ||
        (read(0x3228u, 4u) >> 29) != 1u ||
        write(0x2100u, 0x1000u, 4u) != 0 ||
        writeVram(0x1000u, invalidCallTargetStream, sizeof(invalidCallTargetStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(invalidCallTargetStream),
                      &dmaGet, &dmaMethods) == 0 ||
        (read(0x2100u, 4u) & 0x1000u) == 0u ||
        (read(0x3228u, 4u) >> 29) != 6u ||
        write(0x2100u, 0x1000u, 4u) != 0 ||
        writeVram(0x1000u, inactiveReturnStream, sizeof(inactiveReturnStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(inactiveReturnStream),
                      &dmaGet, &dmaMethods) == 0 ||
        (read(0x2100u, 4u) & 0x1000u) == 0u ||
        (read(0x3228u, 4u) >> 29) != 3u ||
        write(0x2100u, 0x1000u, 4u) != 0) {
        std::cerr << "FAIL: PFIFO CALL/RETURN error state and interrupt reporting\\n";
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


    /* Bind DMA_A as a texture source and decode its mip chain from VRAM. */
    const uint32_t textureContextStream[] = {
        (1u << 18) | 0x0180u, dmaHandle,
        (1u << 18) | 0x0184u, dmaHandle,
        (1u << 18) | 0x0188u, dmaHandle,
        (1u << 18) | 0x018Cu, dmaHandle
    };
    const uint8_t textureMipSource[20] = {
        0x03,0x02,0x01,0xFF, 0x06,0x05,0x04,0xFF,
        0x09,0x08,0x07,0xFF, 0x0C,0x0B,0x0A,0xFF,
        0x0F,0x0E,0x0D,0xFF
    };
    uint8_t textureMipOutput[20]{};
    uint32_t textureMipOffsets[2]{}, textureMipSizes[2]{};
    DmaInfo textureDmaContext{};
    if (writeVram(0x1000u, textureContextStream, sizeof(textureContextStream)) != 0 ||
        submitDmaPush(channel, dmaHandle, 0u, sizeof(textureContextStream),
                      &dmaGet, &dmaMethods) != 0 ||
        getDmaContext(channel, 0u, 0x0184u, &textureDmaContext) != 0 ||
        textureDmaContext.handle != dmaHandle ||
        writeVram(0x1400u, textureMipSource, sizeof(textureMipSource)) != 0 ||
        decodeMipChainFromDma(channel, 0u, 0x0184u, 0x400u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) != 0 ||
        textureMipOffsets[1] != 16u || textureMipSizes[1] != 4u ||
        textureMipOutput[0] != 1u || textureMipOutput[16] != 13u ||
        decodeMipChainFromDma(channel, 0u, 0x0180u, 0x400u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) != 0 ||
        decodeMipChainFromDma(channel, 0u, 0x0188u, 0x400u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) != 0 ||
        decodeMipChainFromDma(channel, 0u, 0x018Cu, 0x400u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) != 0 ||
        decodeMipChainFromDma(channel, 0u, 0x0190u, 0x400u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) == 0 ||
        decodeMipChainFromDma(channel, 0u, 0x0184u, 0x0FF0u, 0x0600u,
            2u, 2u, 2u, textureMipOutput, sizeof(textureMipOutput),
            textureMipOffsets, textureMipSizes) == 0) {
        std::cerr << "FAIL: DMA-bound texture mip-chain fetch/decode\\n";
        stop();
        return 1;
    }


    /* Linear-image DMA decode honors the guest's padded row pitch. */
    const uint8_t pitchedTexture[20] = {
        0x03,0x02,0x01,0xFF, 0x06,0x05,0x04,0xFF, 0xDE,0xAD,0xBE,0xEF,
        0x09,0x08,0x07,0xFF, 0x0C,0x0B,0x0A,0xFF
    };
    uint8_t pitchedOutput[16]{};
    uint32_t pitchedOutputStride = 0;
    if (writeVram(0x1500u, pitchedTexture, sizeof(pitchedTexture)) != 0 ||
        decodeTextureFromDma(channel, 0u, 0x0184u, 0x500u, 0x1200u,
            2u, 2u, 12u, pitchedOutput, sizeof(pitchedOutput),
            &pitchedOutputStride) != 0 ||
        pitchedOutputStride != 8u ||
        pitchedOutput[0] != 1u || pitchedOutput[1] != 2u ||
        pitchedOutput[2] != 3u || pitchedOutput[3] != 0xFFu ||
        pitchedOutput[8] != 7u || pitchedOutput[9] != 8u ||
        pitchedOutput[10] != 9u || pitchedOutput[11] != 0xFFu ||
        decodeTextureFromDma(channel, 0u, 0x0184u, 0x500u, 0x1200u,
            2u, 2u, 7u, pitchedOutput, sizeof(pitchedOutput),
            &pitchedOutputStride) == 0 ||
        decodeTextureFromDma(channel, 0u, 0x0184u, 0x500u, 0x1200u,
            2u, 2u, 12u, pitchedOutput, 15u, &pitchedOutputStride) == 0) {
        std::cerr << "FAIL: pitched linear texture DMA decode\\n";
        stop();
        return 1;
    }


    /* Derive the active stage shape/context from the real NV097 registers. */
    const uint32_t textureFormat = 1u | (2u << 4) | (0x06u << 8) |
        (2u << 16) | (1u << 20) | (1u << 24);
    if (submit(0u, 0x1B00u, 0x400u) != 0 ||
        submit(0u, 0x1B04u, textureFormat) != 0 ||
        submit(0u, 0x1B0Cu, (1u << 30) | (1u << 6)) != 0)
        return 1;
    TextureInfo textureInfo{};
    uint8_t stagePixels[20]{};
    uint32_t stageOffsets[2]{}, stageSizes[2]{};
    if (getTextureInfo(channel, 0u, 0u, &textureInfo) != 0 ||
        textureInfo.texture_offset != 0x400u || textureInfo.width != 2u ||
        textureInfo.height != 2u || textureInfo.level_count != 2u ||
        textureInfo.context_method != 0x0184u || !textureInfo.enabled ||
        textureInfo.linear ||
        decodeTextureStage(channel, 0u, 0u, stagePixels, sizeof(stagePixels),
            stageOffsets, stageSizes) != 0 ||
        stageOffsets[1] != 16u || stageSizes[1] != 4u ||
        stagePixels[0] != 1u || stagePixels[16] != 13u) {
        std::cerr << "FAIL: live NV097 texture stage decode\\n";
        stop();
        return 1;
    }

    /* Linear stage dimensions and row pitch come from IMAGE_RECT/CONTROL1. */
    const uint32_t linearStageFormat = 1u | (2u << 4) | (0x12u << 8) | (1u << 16);
    TextureInfo linearStageInfo{};
    uint8_t linearStagePixels[16]{};
    uint32_t linearStageOffsets[1]{}, linearStageSizes[1]{};
    if (submit(0u, 0x1B00u, 0x500u) != 0 ||
        submit(0u, 0x1B04u, linearStageFormat) != 0 ||
        submit(0u, 0x1B0Cu, (1u << 30)) != 0 ||
        submit(0u, 0x1B10u, (12u << 16)) != 0 ||
        submit(0u, 0x1B1Cu, (2u << 16) | 2u) != 0 ||
        getTextureInfo(channel, 0u, 0u, &linearStageInfo) != 0 ||
        !linearStageInfo.linear || linearStageInfo.width != 2u ||
        linearStageInfo.height != 2u || linearStageInfo.level_count != 1u ||
        linearStageInfo.source_pitch != 12u ||
        decodeTextureStage(channel, 0u, 0u, linearStagePixels,
            sizeof(linearStagePixels), linearStageOffsets,
            linearStageSizes) != 0 ||
        linearStageOffsets[0] != 0u || linearStageSizes[0] != 16u ||
        linearStagePixels[0] != 1u || linearStagePixels[8] != 7u ||
        submit(0u, 0x1B04u, 1u | (2u << 4) | (0x17u << 8) | (1u << 16)) != 0 ||
        getTextureInfo(channel, 0u, 0u, &linearStageInfo) == 0 ||
        submit(0u, 0x1B04u, textureFormat | (1u << 2)) != 0 ||
        getTextureInfo(channel, 0u, 0u, &linearStageInfo) == 0 ||
        submit(0u, 0x1B04u, textureFormat | (1u << 3)) != 0 ||
        getTextureInfo(channel, 0u, 0u, &linearStageInfo) == 0) {
        std::cerr << "FAIL: live linear NV097 texture stage decode\\n";
        stop();
        return 1;
    }

    /* I8_A8R8G8B8 palette uses DMA_A independently of texture DMA_B. */
    uint8_t paletteBytes[1024]{};
    const uint8_t paletteEntries[16] = {
        0x30,0x20,0x10,0xFF, 0x60,0x50,0x40,0xFF,
        0x90,0x80,0x70,0xFF, 0xC0,0xB0,0xA0,0xFF
    };
    memcpy(paletteBytes,paletteEntries,sizeof(paletteEntries));
    const uint8_t paletteIndices[4] = {0u,1u,2u,3u};
    const uint32_t palettedFormat = 1u | (2u<<4) | (0x0Bu<<8) |
        (1u<<16) | (1u<<20) | (1u<<24);
    uint8_t palettedPixels[16]{};
    uint32_t palettedOffsets[1]{},palettedSizes[1]{};
    if(writeVram(0x1600u,paletteBytes,sizeof(paletteBytes))!=0||
       writeVram(0x1700u,paletteIndices,sizeof(paletteIndices))!=0||
       submit(0u,0x1B00u,0x700u)!=0||
       submit(0u,0x1B04u,palettedFormat)!=0||
       submit(0u,0x1B0Cu,(1u<<30))!=0||
       submit(0u,0x1B10u,0u)!=0||
       submit(0u,0x1B20u,0x600u)!=0||
       decodeTextureStage(channel,0u,0u,palettedPixels,sizeof(palettedPixels),
           palettedOffsets,palettedSizes)!=0||
       palettedSizes[0]!=16u||
       palettedPixels[0]!=0x10u||palettedPixels[1]!=0x20u||
       palettedPixels[2]!=0x30u||palettedPixels[3]!=0xFFu||
       palettedPixels[4]!=0x40u||palettedPixels[5]!=0x50u||
       palettedPixels[6]!=0x60u||palettedPixels[7]!=0xFFu||
       palettedPixels[8]!=0x70u||palettedPixels[9]!=0x80u||
       palettedPixels[10]!=0x90u||palettedPixels[11]!=0xFFu||
       palettedPixels[12]!=0xA0u||palettedPixels[13]!=0xB0u||
       palettedPixels[14]!=0xC0u||palettedPixels[15]!=0xFFu){
        std::cerr<<"FAIL: paletted NV097 texture DMA decode\\n";
        stop();return 1;
    }

    /* Palette DMA_B selection and 32-entry index bounds are checked too. */
    const uint8_t invalidPaletteIndices[4] = {0u,1u,2u,32u};
    if(submit(0u,0x1B20u,0x601u)!=0||
       decodeTextureStage(channel,0u,0u,palettedPixels,sizeof(palettedPixels),
           palettedOffsets,palettedSizes)!=0||
       writeVram(0x1700u,invalidPaletteIndices,sizeof(invalidPaletteIndices))!=0||
       submit(0u,0x1B20u,0x60Du)!=0||
       decodeTextureStage(channel,0u,0u,palettedPixels,sizeof(palettedPixels),
           palettedOffsets,palettedSizes)==0){
        std::cerr<<"FAIL: palette DMA selector/length validation\\n";
        stop();return 1;
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

    /* ARRAY_ELEMENT16 accumulation must not overflow at the old 255-index cap. */
    {
        Stats beforeBatch{}, afterBatch{};
        if (getStats(&beforeBatch) != 0 ||
            submit(0u, 0x17FCu, 5u) != 0) {
            std::cerr << "FAIL: begin large indexed batch\\n";
            stop();
            return 1;
        }
        for (uint32_t i = 0; i < 300u; ++i) {
            if (submit(0u, 0x1800u, i & 1u) != 0) {
                std::cerr << "FAIL: submit large indexed batch element\\n";
                stop();
                return 1;
            }
        }
        if (submit(0u, 0x17FCu, 0u) != 0 ||
            getStats(&afterBatch) != 0 ||
            afterBatch.methods_handled < beforeBatch.methods_handled + 300u) {
            std::cerr << "FAIL: PGRAPH dropped indexed elements above 255\\n";
            stop();
            return 1;
        }
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
