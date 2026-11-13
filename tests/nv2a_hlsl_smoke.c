#define COBJMACROS
#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>
#include "d3d8_vsh.h"

static int compile_program(const DWORD* instructions, unsigned count,
                           const char* label)
{
    NV2AVshProgram program;
    char hlsl[16384];
    ID3DBlob* bytecode = NULL;
    ID3DBlob* errors = NULL;

    memset(&program, 0, sizeof(program));
    d3d8_vsh_parse(instructions, count, &program);

    const int length = d3d8_vsh_generate_hlsl(
        &program, hlsl, (int)sizeof(hlsl));
    if (length <= 0 || length >= (int)sizeof(hlsl)) {
        fprintf(stderr, "FAIL: %s HLSL generation failed\\n", label);
        return 1;
    }
    if (!strstr(hlsl, "SV_POSITION")) {
        fprintf(stderr, "FAIL: %s generated HLSL has no position output\\n",
                label);
        return 2;
    }

    const HRESULT hr = D3DCompile(
        hlsl, (SIZE_T)length, label, NULL, NULL, "main", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if (FAILED(hr)) {
        if (errors && ID3D10Blob_GetBufferPointer(errors))
            fputs((const char*)ID3D10Blob_GetBufferPointer(errors), stderr);
        fputs("\\n--- generated HLSL ---\\n", stderr);
        fputs(hlsl, stderr);
        fputs("\\n", stderr);
        if (errors) ID3D10Blob_Release(errors);
        if (bytecode) ID3D10Blob_Release(bytecode);
        fprintf(stderr, "FAIL: %s did not compile as VS 5.0\\n", label);
        return 3;
    }

    if (errors) ID3D10Blob_Release(errors);
    if (!bytecode || ID3D10Blob_GetBufferSize(bytecode) == 0) {
        fprintf(stderr, "FAIL: %s compiler returned empty bytecode\\n", label);
        if (bytecode) ID3D10Blob_Release(bytecode);
        return 4;
    }
    ID3D10Blob_Release(bytecode);
    printf("PASS: %s HLSL generation and VS 5.0 compilation\\n", label);
    return 0;
}

int main(void)
{
    DWORD instruction[4] = {0, 0, 0, 0};

    /* Keep the one-slot NOP case as the smallest parser/compiler sanity check. */
    instruction[3] = 1u;
    if (compile_program(instruction, 1u, "NV2A NOP") != 0)
        return 1;

    /* MAC MOV v0 -> oPos exercises a real input read and position write. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (1u << 21) | 0x1Bu;
    instruction[2] = 2u << 26;
    instruction[3] = (0xFu << 12) | (1u << 11) | 1u;
    if (compile_program(instruction, 1u, "NV2A MOV input-to-oPos") != 0)
        return 2;

    /* MAC MOV c[5] -> oPos exercises constant-register source emission. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (1u << 21) | (5u << 13) | 0x1Bu;
    instruction[2] = 3u << 26;
    instruction[3] = (0xFu << 12) | (1u << 11) | 1u;
    if (compile_program(instruction, 1u, "NV2A MOV constant-to-oPos") != 0)
        return 3;

    puts("PASS: NV2A NOP, input MOV, and constant MOV shader translation");
    return 0;
}
