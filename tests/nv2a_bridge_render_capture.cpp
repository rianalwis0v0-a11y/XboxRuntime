#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "d3d8_xbox.h"

namespace {
constexpr uint32_t kChannel = 2u;
constexpr uint32_t kDmaHandle = 0x12345678u;
constexpr uint32_t kDmaInstance = 0x100u;
constexpr uint32_t kDmaBase = 0x1000u;
constexpr uint32_t kVertexOffset = 0x300u;
constexpr uint32_t kSurfaceOffset = 0x00100000u;
constexpr uint32_t kWidth = 320u;
constexpr uint32_t kHeight = 240u;
constexpr uint32_t kSurfacePitch = kWidth * 4u;

using StartFn = int (__cdecl*)(void);
using StopFn = void (__cdecl*)(void);
using SetDeviceFn = int (__cdecl*)(void*);
using WriteRAMINFn = int (__cdecl*)(uint32_t,const void*,uint32_t);
using WriteVRAMFn = int (__cdecl*)(uint32_t,const void*,uint32_t);
using ReadVRAMFn = int (__cdecl*)(uint32_t,void*,uint32_t);
using WriteRegisterFn = int (__cdecl*)(uint32_t,uint64_t,uint32_t);
using SubmitDMAPushBufferFn = int (__cdecl*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*,uint32_t*);
using FlushFn = void (__cdecl*)(void);
struct Stats {
    uint32_t frames;
    uint32_t draw_calls;
    uint32_t vertices_submitted;
    uint32_t methods_handled;
    uint32_t methods_ignored;
    uint32_t clears;
};
using GetStatsFn = int (__cdecl*)(Stats*);

template<typename T>
T proc(HMODULE module, const char* name)
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

void append_method(std::vector<uint32_t>& stream, uint32_t method, uint32_t value)
{
    stream.push_back((1u << 18) | method);
    stream.push_back(value);
}

bool write_bmp(const std::filesystem::path& path,
               const std::vector<uint8_t>& pixels,
               uint32_t width, uint32_t height)
{
#pragma pack(push, 1)
    struct FileHeader {
        uint16_t type;
        uint32_t size;
        uint16_t reserved1;
        uint16_t reserved2;
        uint32_t offset;
    } file{0x4D42u, 54u + width * height * 4u, 0u, 0u, 54u};
    struct InfoHeader {
        uint32_t size;
        int32_t width;
        int32_t height;
        uint16_t planes;
        uint16_t bits;
        uint32_t compression;
        uint32_t image_size;
        int32_t xppm;
        int32_t yppm;
        uint32_t colors;
        uint32_t important;
    } info{40u, (int32_t)width, (int32_t)height, 1u, 32u, 0u,
           width * height * 4u, 2835, 2835, 0u, 0u};
#pragma pack(pop)
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    const size_t row_bytes = (size_t)width * 4u;
    for (uint32_t y = height; y-- > 0u;)
        out.write(reinterpret_cast<const char*>(pixels.data() + (size_t)y * row_bytes),
                  (std::streamsize)row_bytes);
    return out.good();
}
}

int main()
{
    const char* window_class = "XboxRuntimeNV2ABridgeCapture";
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = window_class;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        std::fprintf(stderr, "FAIL: RegisterClassA (%lu)\n", GetLastError());
        return 1;
    }
    HWND hwnd = CreateWindowExA(0, window_class, "NV2A bridge capture",
        WS_OVERLAPPEDWINDOW, 0, 0, (int)kWidth, (int)kHeight,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "FAIL: CreateWindowExA (%lu)\n", GetLastError());
        return 1;
    }

    IDirect3D8* d3d = xbox_Direct3DCreate8(220u);
    if (!d3d) {
        std::fprintf(stderr, "FAIL: xbox_Direct3DCreate8\n");
        DestroyWindow(hwnd);
        return 1;
    }
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = kWidth;
    pp.BackBufferHeight = kHeight;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1u;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = FALSE;
    pp.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice8* device = nullptr;
    HRESULT hr = d3d->lpVtbl->CreateDevice(d3d, 0u, D3DDEVTYPE_HAL,
        hwnd, 0u, &pp, &device);
    if (FAILED(hr) || !device) {
        std::fprintf(stderr, "FAIL: D3D8 CreateDevice HRESULT=0x%08lX\n",
                     (unsigned long)hr);
        d3d->lpVtbl->Release(d3d);
        DestroyWindow(hwnd);
        return 1;
    }

    HMODULE module = LoadLibraryA("nv2a.dll");
    if (!module) {
        std::fprintf(stderr, "FAIL: LoadLibraryA(nv2a.dll) error=%lu\n", GetLastError());
        device->lpVtbl->Release(device);
        d3d->lpVtbl->Release(d3d);
        DestroyWindow(hwnd);
        return 1;
    }
    const auto start = proc<StartFn>(module, "ModuleStart");
    const auto stop = proc<StopFn>(module, "ModuleStop");
    const auto set_device = proc<SetDeviceFn>(module, "XboxNV2A_SetD3DDevice");
    const auto write_ramin = proc<WriteRAMINFn>(module, "XboxNV2A_WriteRAMIN");
    const auto write_vram = proc<WriteVRAMFn>(module, "XboxNV2A_WriteVRAM");
    const auto read_vram = proc<ReadVRAMFn>(module, "XboxNV2A_ReadVRAM");
    const auto write_register = proc<WriteRegisterFn>(module, "XboxNV2A_WriteRegister");
    const auto submit_dma = proc<SubmitDMAPushBufferFn>(module, "XboxNV2A_SubmitDMAPushBuffer");
    const auto flush = proc<FlushFn>(module, "XboxNV2A_Flush");
    const auto get_stats = proc<GetStatsFn>(module, "XboxNV2A_GetStats");
    if (!start || !stop || !set_device || !write_ramin || !write_vram ||
        !read_vram || !write_register || !submit_dma || !flush || !get_stats) {
        std::fprintf(stderr, "FAIL: NV2A bridge exports missing\n");
        FreeLibrary(module);
        device->lpVtbl->Release(device);
        d3d->lpVtbl->Release(d3d);
        DestroyWindow(hwnd);
        return 1;
    }
    if (set_device(device) != 0 || start() != 0) {
        std::fprintf(stderr, "FAIL: NV2A module/device initialization\n");
        stop();
        FreeLibrary(module);
        device->lpVtbl->Release(device);
        d3d->lpVtbl->Release(d3d);
        DestroyWindow(hwnd);
        return 1;
    }

    uint32_t ramht_hash = 0;
    for (uint32_t value = kDmaHandle; value; value >>= 9)
        ramht_hash ^= value & 0x1FFu;
    ramht_hash ^= kChannel << 5;
    ramht_hash &= 0x1FFu;
    const uint32_t ramht_entry[2] = {
        kDmaHandle,
        0x80000000u | (kChannel << 24) | (1u << 16) | (kDmaInstance >> 4)
    };
    const uint32_t dma_object[3] = {0x00000002u, 0x00000FFFu, kDmaBase};
    if (write_register(0x2210u, 0u, 4u) != 0 ||
        write_ramin(ramht_hash * 8u, ramht_entry, sizeof(ramht_entry)) != 0 ||
        write_ramin(kDmaInstance, dma_object, sizeof(dma_object)) != 0) {
        std::fprintf(stderr, "FAIL: RAMHT/DMA object setup\n");
        stop(); FreeLibrary(module); device->lpVtbl->Release(device);
        d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
    }

    auto submit_stream = [&](const std::vector<uint32_t>& stream) -> bool {
        const uint32_t bytes = (uint32_t)(stream.size() * sizeof(uint32_t));
        uint32_t new_get = 0, methods = 0;
        if (bytes > 0x300u ||
            write_vram(kDmaBase, stream.data(), bytes) != 0 ||
            submit_dma(kChannel, kDmaHandle, 0u, bytes, &new_get, &methods) != 0 ||
            new_get != bytes || methods == 0u) {
            std::fprintf(stderr, "FAIL: DMA pushbuffer get=%u put=%u methods=%u\n",
                         new_get, bytes, methods);
            return false;
        }
        return true;
    };

    std::vector<uint32_t> setup;
    append_method(setup, 0x0000u, kDmaHandle);                 // bind PGRAPH object
    append_method(setup, 0x0180u, kDmaHandle);                 // bind DMA_A vertex memory
    append_method(setup, 0x1D88u, 0x00000106u);                // linear A8R8G8B8 surface
    append_method(setup, 0x1D8Cu, kSurfacePitch);              // color pitch
    append_method(setup, 0x1D90u, kSurfaceOffset);             // color offset
    append_method(setup, 0x1D94u, 0u);                         // no zeta surface
    append_method(setup, 0x1D98u, kWidth << 16);               // clip width
    append_method(setup, 0x1D9Cu, kHeight << 16);              // clip height
    append_method(setup, 0x1E94u, 2u);                         // programmable GPU mode
    append_method(setup, 0x1E98u, 0u);                         // no context writes
    append_method(setup, 0x1E9Cu, 0u);                         // program load slot 0
    const uint32_t program[8] = {
        0u, (1u << 21), (2u << 26), (0xFu << 12) | (1u << 11),
        0u, (1u << 21), (3u << 26), (0xFu << 12) | (1u << 11) | (3u << 3) | 1u
    };
    for (uint32_t word : program)
        append_method(setup, 0x0B00u, word);                   // auto-increment program words
    append_method(setup, 0x1EA0u, 0u);                         // start slot 0
    append_method(setup, 0x1EA4u, 0u);                         // constant load c0
    for (unsigned i = 0; i < 4u; ++i)
        append_method(setup, 0x0B80u, 0x3F800000u);             // c0 = (1,1,1,1)
    append_method(setup, 0x1720u, kVertexOffset);              // position array offset
    append_method(setup, 0x1760u, (12u << 8) | (3u << 4) | 2u);// FLOAT3, stride 12
    append_method(setup, 0x17FCu, 5u);                         // initialize surface target
    append_method(setup, 0x17FCu, 0u);
    if (!submit_stream(setup)) {
        stop(); FreeLibrary(module); device->lpVtbl->Release(device);
        d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
    }

    std::filesystem::create_directories("nv2a_bridge_frames");
    std::vector<uint8_t> pixels((size_t)kWidth * kHeight * 4u);
    bool saw_triangle = false;
    for (uint32_t frame = 0; frame < 10u; ++frame) {
        const float shift = ((float)frame - 4.5f) * 0.035f;
        const float vertices[9] = {
            -0.60f + shift, -0.55f, 0.50f,
             0.00f + shift,  0.60f, 0.50f,
             0.60f + shift, -0.55f, 0.50f
        };
        if (write_vram(kDmaBase + kVertexOffset, vertices, sizeof(vertices)) != 0 ||
            FAILED(device->lpVtbl->BeginScene(device)) ||
            FAILED(device->lpVtbl->Clear(device, 0u, nullptr, 1u,
                0xFF101820u, 1.0f, 0u))) {
            std::fprintf(stderr, "FAIL: begin/clear frame %u\n", frame);
            stop(); FreeLibrary(module); device->lpVtbl->Release(device);
            d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
        }

        std::vector<uint32_t> draw;
        append_method(draw, 0x0000u, kDmaHandle);
        append_method(draw, 0x0180u, kDmaHandle);
        append_method(draw, 0x17FCu, 5u);                       // triangles
        append_method(draw, 0x1810u, (3u << 24));               // DRAW_ARRAYS(0,3)
        append_method(draw, 0x17FCu, 0u);                       // end
        const bool submitted = submit_stream(draw);
        const HRESULT end_result = device->lpVtbl->EndScene(device);
        if (!submitted || FAILED(end_result)) {
            std::fprintf(stderr, "FAIL: NV2A bridge draw frame %u\n", frame);
            stop(); FreeLibrary(module); device->lpVtbl->Release(device);
            d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
        }
        flush();
        if (read_vram(kSurfaceOffset, pixels.data(), (uint32_t)pixels.size()) != 0) {
            std::fprintf(stderr, "FAIL: readback frame %u\n", frame);
            stop(); FreeLibrary(module); device->lpVtbl->Release(device);
            d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
        }
        for (size_t p = 0; p < pixels.size(); p += 4u) {
            if (pixels[p] > 200u && pixels[p + 1u] > 200u && pixels[p + 2u] > 200u) {
                saw_triangle = true;
                break;
            }
        }
        char filename[64];
        std::snprintf(filename, sizeof(filename), "nv2a_bridge_frames/frame_%02u.bmp", frame);
        if (!write_bmp(filename, pixels, kWidth, kHeight)) {
            std::fprintf(stderr, "FAIL: write frame %u\n", frame);
            stop(); FreeLibrary(module); device->lpVtbl->Release(device);
            d3d->lpVtbl->Release(d3d); DestroyWindow(hwnd); return 1;
        }
    }

    Stats stats{};
    const bool stats_ok = get_stats(&stats) == 0;
    stop();
    FreeLibrary(module);
    device->lpVtbl->Release(device);
    d3d->lpVtbl->Release(d3d);
    DestroyWindow(hwnd);
    if (!saw_triangle || !stats_ok || stats.draw_calls < 10u) {
        std::fprintf(stderr, "FAIL: NV2A bridge did not render 10 frames (draws=%llu triangle=%d)\n",
                     (unsigned long long)stats.draw_calls, saw_triangle ? 1 : 0);
        return 1;
    }
    std::printf("PASS: 10 frames rendered through nv2a.dll D3D8 bridge (draws=%llu)\n",
                (unsigned long long)stats.draw_calls);
    return 0;
}
