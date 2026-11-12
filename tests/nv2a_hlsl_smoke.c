#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>
#include "d3d8_vsh.h"

int main(void)
{
    DWORD instruction[4] = {0, 0, 0, 0};
    NV2AVshProgram program;
    char hlsl[16384];
    ID3DBlob* bytecode = NULL;
    ID3DBlob* errors = NULL;

    /* MOV v0 -> oPos, all components, FINAL. */
    instruction[1] = (1u << 21) | 0x1Bu;
    instruction[2] = 2u << 26;
    instruction[3] = (0xFu << 12) | (1u << 11) | 1u;

    memset(&program, 0, sizeof(program));
    d3d8_vsh_parse(instruction, 1, &program);
    if (!(program.inputs_read & 1u)) {
        fputs("FAIL: translator did not detect v0 input\n", stderr);
        return 1;
    }

    const int length = d3d8_vsh_generate_hlsl(&program, hlsl, (int)sizeof(hlsl));
    if (length <= 0 || length >= (int)sizeof(hlsl)) {
        fputs("FAIL: HLSL generation failed\n", stderr);
        return 2;
    }

    const HRESULT hr = D3DCompile(hlsl, (SIZE_T)length, "nv2a_hlsl_smoke",
        NULL, NULL, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &bytecode, &errors);
    if (FAILED(hr)) {
        if (errors && ID3D10Blob_GetBufferPointer(errors))
            fputs((const char*)ID3D10Blob_GetBufferPointer(errors), stderr);
        fputs("\n--- generated HLSL ---\n", stderr);
        fputs(hlsl, stderr);
        fputs("\n", stderr);
        if (errors) ID3D10Blob_Release(errors);
        return 3;
    }

    if (errors) ID3D10Blob_Release(errors);
    if (!bytecode || ID3D10Blob_GetBufferSize(bytecode) == 0) {
        fputs("FAIL: compiler returned empty shader bytecode\n", stderr);
        if (bytecode) ID3D10Blob_Release(bytecode);
        return 4;
    }
    ID3D10Blob_Release(bytecode);
    puts("PASS: NV2A microcode translated to HLSL and compiled to VS 5.0 bytecode");
    return 0;
}
