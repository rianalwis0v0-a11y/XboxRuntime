#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d8_vsh.h"

#define FRAME_WIDTH 160
#define FRAME_HEIGHT 120
#define FRAME_COUNT 10

#pragma pack(push, 1)
typedef struct BmpFileHeader {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t pixel_offset;
} BmpFileHeader;

typedef struct BmpInfoHeader {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_meter;
    int32_t y_pixels_per_meter;
    uint32_t colors_used;
    uint32_t important_colors;
} BmpInfoHeader;
#pragma pack(pop)

typedef struct Vertex {
    float position[4];
} Vertex;

static int save_bmp(const char *path, const D3D11_MAPPED_SUBRESOURCE *mapped,
                    unsigned *colored_pixels)
{
    BmpFileHeader file_header;
    BmpInfoHeader info_header;
    const uint32_t row_bytes = FRAME_WIDTH * 3u;
    const uint32_t row_stride = (row_bytes + 3u) & ~3u;
    uint8_t row[FRAME_WIDTH * 3u + 4u];
    FILE *file;
    unsigned count = 0;
    int y, x;

    memset(&file_header, 0, sizeof(file_header));
    memset(&info_header, 0, sizeof(info_header));
    file_header.type = 0x4D42u;
    file_header.pixel_offset = (uint32_t)(sizeof(file_header) + sizeof(info_header));
    file_header.size = file_header.pixel_offset + row_stride * FRAME_HEIGHT;
    info_header.size = sizeof(info_header);
    info_header.width = FRAME_WIDTH;
    info_header.height = FRAME_HEIGHT;
    info_header.planes = 1;
    info_header.bits_per_pixel = 24;
    info_header.compression = 0;
    info_header.image_size = row_stride * FRAME_HEIGHT;

    file = fopen(path, "wb");
    if (!file)
        return 0;
    if (fwrite(&file_header, sizeof(file_header), 1, file) != 1 ||
        fwrite(&info_header, sizeof(info_header), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    for (y = FRAME_HEIGHT - 1; y >= 0; --y) {
        const uint8_t *src = (const uint8_t *)mapped->pData +
                             (size_t)y * mapped->RowPitch;
        memset(row, 0, row_stride);
        for (x = 0; x < FRAME_WIDTH; ++x) {
            const uint8_t red = src[x * 4u + 0u];
            const uint8_t green = src[x * 4u + 1u];
            const uint8_t blue = src[x * 4u + 2u];
            row[x * 3u + 0u] = blue;
            row[x * 3u + 1u] = green;
            row[x * 3u + 2u] = red;
            if (red > 20u && green > 100u && blue > 150u)
                ++count;
        }
        if (fwrite(row, row_stride, 1, file) != 1) {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    if (colored_pixels)
        *colored_pixels = count;
    return 1;
}

static int compile_shader(const char *source, SIZE_T source_size,
                          const char *entry, const char *profile,
                          ID3DBlob **bytecode)
{
    ID3DBlob *errors = NULL;
    HRESULT hr = D3DCompile(source, source_size, "nv2a_render_capture",
                            NULL, NULL, entry, profile,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                            bytecode, &errors);
    if (FAILED(hr)) {
        if (errors && ID3D10Blob_GetBufferPointer(errors))
            fputs((const char *)ID3D10Blob_GetBufferPointer(errors), stderr);
    }
    if (errors)
        ID3D10Blob_Release(errors);
    return SUCCEEDED(hr);
}

int main(void)
{
    static const char pixel_shader_source[] =
        "float4 main() : SV_TARGET { "
        "return float4(0.12, 0.78, 0.98, 1.0); }\n";
    DWORD microcode[4] = {0, 0, 0, 0};
    NV2AVshProgram program;
    char vertex_hlsl[16384];
    int vertex_hlsl_length;
    ID3DBlob *vertex_bytecode = NULL;
    ID3DBlob *pixel_bytecode = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11VertexShader *vertex_shader = NULL;
    ID3D11PixelShader *pixel_shader = NULL;
    ID3D11InputLayout *input_layout = NULL;
    ID3D11Buffer *vertex_buffer = NULL;
    ID3D11Texture2D *render_texture = NULL;
    ID3D11Texture2D *staging_texture = NULL;
    ID3D11RenderTargetView *render_target = NULL;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_9_1;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D11_TEXTURE2D_DESC texture_desc;
    D3D11_TEXTURE2D_DESC staging_desc;
    D3D11_BUFFER_DESC buffer_desc;
    D3D11_INPUT_ELEMENT_DESC input_element;
    D3D11_VIEWPORT viewport;
    D3D11_MAPPED_SUBRESOURCE mapped;
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    float clear_color[4] = {0.02f, 0.025f, 0.035f, 1.0f};
    const char *driver_name = "hardware";
    HRESULT hr;
    unsigned frame;
    int result = 1;

    /* A real NV2A MOV v0 -> oPos instruction, translated to VS 5.0 HLSL. */
    microcode[1] = (1u << 21) | 0x1Bu;
    microcode[2] = 2u << 26;
    microcode[3] = (0xFu << 12) | (1u << 11) | 1u;
    memset(&program, 0, sizeof(program));
    d3d8_vsh_parse(microcode, 1, &program);
    vertex_hlsl_length = d3d8_vsh_generate_hlsl(
        &program, vertex_hlsl, (int)sizeof(vertex_hlsl));
    if (vertex_hlsl_length <= 0 ||
        vertex_hlsl_length >= (int)sizeof(vertex_hlsl)) {
        fputs("FAIL: NV2A vertex microcode did not generate HLSL\n", stderr);
        goto cleanup;
    }
    if (!compile_shader(vertex_hlsl, (SIZE_T)vertex_hlsl_length,
                        "main", "vs_5_0", &vertex_bytecode) ||
        !compile_shader(pixel_shader_source, sizeof(pixel_shader_source) - 1u,
                        "main", "ps_5_0", &pixel_bytecode)) {
        fputs("FAIL: D3DCompile rejected the NV2A render shaders\n", stderr);
        goto cleanup;
    }

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                           &device, &feature_level, &context);
    if (FAILED(hr) || feature_level < D3D_FEATURE_LEVEL_11_0) {
        if (context) { ID3D11DeviceContext_Release(context); context = NULL; }
        if (device) { ID3D11Device_Release(device); device = NULL; }
        driver_name = "WARP";
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               &device, &feature_level, &context);
    }
    if (FAILED(hr) || !device || !context ||
        feature_level < D3D_FEATURE_LEVEL_11_0) {
        fputs("FAIL: D3D11 feature level 11.0 device creation failed\n", stderr);
        goto cleanup;
    }

    hr = ID3D11Device_CreateVertexShader(
        device, ID3D10Blob_GetBufferPointer(vertex_bytecode),
        ID3D10Blob_GetBufferSize(vertex_bytecode), NULL, &vertex_shader);
    if (FAILED(hr))
        goto cleanup;
    hr = ID3D11Device_CreatePixelShader(
        device, ID3D10Blob_GetBufferPointer(pixel_bytecode),
        ID3D10Blob_GetBufferSize(pixel_bytecode), NULL, &pixel_shader);
    if (FAILED(hr))
        goto cleanup;

    memset(&input_element, 0, sizeof(input_element));
    input_element.SemanticName = "ATTR";
    input_element.SemanticIndex = 0;
    input_element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    input_element.InputSlot = 0;
    input_element.AlignedByteOffset = 0;
    input_element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    hr = ID3D11Device_CreateInputLayout(
        device, &input_element, 1,
        ID3D10Blob_GetBufferPointer(vertex_bytecode),
        ID3D10Blob_GetBufferSize(vertex_bytecode), &input_layout);
    if (FAILED(hr))
        goto cleanup;

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.ByteWidth = (UINT)(sizeof(Vertex) * 3u);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &vertex_buffer);
    if (FAILED(hr))
        goto cleanup;

    memset(&texture_desc, 0, sizeof(texture_desc));
    texture_desc.Width = FRAME_WIDTH;
    texture_desc.Height = FRAME_HEIGHT;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(
        device, &texture_desc, NULL, &render_texture);
    if (FAILED(hr))
        goto cleanup;
    hr = ID3D11Device_CreateRenderTargetView(
        device, (ID3D11Resource *)render_texture, NULL, &render_target);
    if (FAILED(hr))
        goto cleanup;

    staging_desc = texture_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(
        device, &staging_desc, NULL, &staging_texture);
    if (FAILED(hr))
        goto cleanup;

    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)FRAME_WIDTH;
    viewport.Height = (FLOAT)FRAME_HEIGHT;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &render_target, NULL);
    ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    ID3D11DeviceContext_IASetInputLayout(context, input_layout);
    ID3D11DeviceContext_IASetVertexBuffers(
        context, 0, 1, &vertex_buffer, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(
        context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, vertex_shader, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, pixel_shader, NULL, 0);

    CreateDirectoryA("out", NULL);
    CreateDirectoryA("out/nv2a_frames", NULL);

    for (frame = 0; frame < FRAME_COUNT; ++frame) {
        Vertex vertices[3];
        D3D11_MAPPED_SUBRESOURCE vertex_map;
        float center = -0.62f + (1.24f * (float)frame / 9.0f);
        char path[MAX_PATH];
        unsigned colored_pixels = 0;

        vertices[0].position[0] = center - 0.18f;
        vertices[0].position[1] = -0.42f;
        vertices[0].position[2] = 0.5f;
        vertices[0].position[3] = 1.0f;
        vertices[1].position[0] = center;
        vertices[1].position[1] = 0.42f;
        vertices[1].position[2] = 0.5f;
        vertices[1].position[3] = 1.0f;
        vertices[2].position[0] = center + 0.18f;
        vertices[2].position[1] = -0.42f;
        vertices[2].position[2] = 0.5f;
        vertices[2].position[3] = 1.0f;

        hr = ID3D11DeviceContext_Map(
            context, (ID3D11Resource *)vertex_buffer, 0,
            D3D11_MAP_WRITE_DISCARD, 0, &vertex_map);
        if (FAILED(hr))
            goto cleanup;
        memcpy(vertex_map.pData, vertices, sizeof(vertices));
        ID3D11DeviceContext_Unmap(
            context, (ID3D11Resource *)vertex_buffer, 0);

        ID3D11DeviceContext_ClearRenderTargetView(
            context, render_target, clear_color);
        ID3D11DeviceContext_Draw(context, 3, 0);
        ID3D11DeviceContext_CopyResource(
            context, (ID3D11Resource *)staging_texture,
            (ID3D11Resource *)render_texture);
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(
            context, (ID3D11Resource *)staging_texture, 0,
            D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            goto cleanup;

        snprintf(path, sizeof(path),
                 "out/nv2a_frames/frame_%02u.bmp", frame);
        if (!save_bmp(path, &mapped, &colored_pixels) ||
            colored_pixels < 100u) {
            ID3D11DeviceContext_Unmap(
                context, (ID3D11Resource *)staging_texture, 0);
            fprintf(stderr, "FAIL: frame %u rendered only %u colored pixels\n",
                    frame, colored_pixels);
            goto cleanup;
        }
        ID3D11DeviceContext_Unmap(
            context, (ID3D11Resource *)staging_texture, 0);
        printf("Frame %02u: %u rendered pixels -> %s\n",
               frame, colored_pixels, path);
    }

    printf("PASS: %u NV2A-translated HLSL frames rendered with D3D11 (%s)\n",
           FRAME_COUNT, driver_name);
    result = 0;

cleanup:
    if (staging_texture) ID3D11Texture2D_Release(staging_texture);
    if (render_target) ID3D11RenderTargetView_Release(render_target);
    if (render_texture) ID3D11Texture2D_Release(render_texture);
    if (vertex_buffer) ID3D11Buffer_Release(vertex_buffer);
    if (input_layout) ID3D11InputLayout_Release(input_layout);
    if (pixel_shader) ID3D11PixelShader_Release(pixel_shader);
    if (vertex_shader) ID3D11VertexShader_Release(vertex_shader);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    if (pixel_bytecode) ID3D10Blob_Release(pixel_bytecode);
    if (vertex_bytecode) ID3D10Blob_Release(vertex_bytecode);
    return result;
}
