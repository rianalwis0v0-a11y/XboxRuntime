#define COBJMACROS
#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>
#include "d3d8_vsh.h"

static int compile_program(const char* name, const DWORD* words, int instruction_count,
                           const char* required_source, uint16_t expected_inputs)
{
    NV2AVshProgram program;
    char hlsl[16384];
    ID3DBlob* bytecode = NULL;
    ID3DBlob* errors = NULL;

    memset(&program, 0, sizeof(program));
    d3d8_vsh_parse(words, instruction_count, &program);

    if (program.inputs_read != expected_inputs) {
        fprintf(stderr, "FAIL: %s input-register metadata mismatch (got 0x%04X, expected mask 0x%04X)\n",
                name, program.inputs_read, expected_inputs);
        return 1;
    }

    const int length = d3d8_vsh_generate_hlsl(&program, hlsl, (int)sizeof(hlsl));
    if (length <= 0 || length >= (int)sizeof(hlsl)) {
        fprintf(stderr, "FAIL: %s HLSL generation failed\n", name);
        return 2;
    }
    if (!strstr(hlsl, "SV_POSITION") || !strstr(hlsl, required_source)) {
        fprintf(stderr, "FAIL: %s generated HLSL is missing expected shader code: %s\n",
                name, required_source);
        fputs("--- generated HLSL ---\n", stderr);
        fputs(hlsl, stderr);
        fputs("\n", stderr);
        return 3;
    }

    const HRESULT hr = D3DCompile(hlsl, (SIZE_T)length, name,
        NULL, NULL, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &bytecode, &errors);
    if (FAILED(hr)) {
        if (errors && ID3D10Blob_GetBufferPointer(errors))
            fputs((const char*)ID3D10Blob_GetBufferPointer(errors), stderr);
        fprintf(stderr, "\nFAIL: %s HLSL did not compile\n", name);
        fputs("--- generated HLSL ---\n", stderr);
        fputs(hlsl, stderr);
        fputs("\n", stderr);
        if (errors) ID3D10Blob_Release(errors);
        if (bytecode) ID3D10Blob_Release(bytecode);
        return 4;
    }

    if (errors) ID3D10Blob_Release(errors);
    if (!bytecode || ID3D10Blob_GetBufferSize(bytecode) == 0) {
        fprintf(stderr, "FAIL: %s compiler returned empty shader bytecode\n", name);
        if (bytecode) ID3D10Blob_Release(bytecode);
        return 5;
    }

    ID3D10Blob_Release(bytecode);
    printf("PASS: %s NV2A microcode -> HLSL -> VS 5.0 bytecode\n", name);
    return 0;
}

int main(void)
{
    /* Real MAC MOV: v0 -> oPos, all four components, then FINAL. */
    {
        const DWORD instruction[4] = {
            0u,
            (1u << 21) | 0x1Bu,
            (2u << 26),
            (0xFu << 12) | (1u << 11) | 1u
        };
        const int result = compile_program(
            "nv2a_vsh_input_mov", instruction, 1,
            "oPos = (v0)", (uint16_t)(1u << 0));
        if (result) return 10 + result;
    }

    /* Real MAC ADD: v5 + v5 -> oPos, exercising both source operands. */
    {
        const DWORD instruction[4] = {
            0u,
            (3u << 21) | (5u << 9) | 0x1Bu,
            (2u << 26) | (0x1Bu << 2),
            (2u << 28) | (0xFu << 12) | (1u << 11) | 1u
        };
        const int result = compile_program(
            "nv2a_vsh_input_add", instruction, 1,
            "oPos = ((v5 + v5))", (uint16_t)(1u << 5));
        if (result) return 20 + result;
    }

    /* Real MAC MOV: c[17] -> oPos, exercising constant-source lowering. */
    {
        const DWORD instruction[4] = {
            0u,
            (1u << 21) | (17u << 13) | 0x1Bu,
            (3u << 26),
            (0xFu << 12) | (1u << 11) | 1u
        };
        const int result = compile_program(
            "nv2a_vsh_constant_mov", instruction, 1,
            "oPos = (c[17])", 0);
        if (result) return 30 + result;
    }


    /* Paired MAC+ILU reads source C before either destination is written.
     * MAC overwrites R1 while ILU RCP reads old R1.x; HLSL must snapshot it. */
    {
        const DWORD instruction[4] = {
            0u,
            (1u << 21) | (2u << 25) | (17u << 13) | 0x1Bu,
            (3u << 26) | (0x1Bu << 2),
            (1u << 30) | (1u << 28) | (0xFu << 24) | (1u << 20) |
                (0x8u << 16) | (0xFu << 12) | (1u << 11) | 1u
        };
        const int result = compile_program(
            "nv2a_vsh_paired_mac_ilu", instruction, 1,
            "_vsh_src2 = R1", 0);
        if (result) return 40 + result;

        NV2AVshProgram program;
        char hlsl[16384];
        memset(&program, 0, sizeof(program));
        d3d8_vsh_parse(instruction, 1, &program);
        const int length = d3d8_vsh_generate_hlsl(&program, hlsl, (int)sizeof(hlsl));
        if (length <= 0 || length >= (int)sizeof(hlsl)) return 50;
        const char* snapshot = strstr(hlsl, "float4 _vsh_src2 = R1;");
        const char* mac_write = strstr(hlsl, "R1 = (c[17])");
        const char* ilu_read = strstr(hlsl, "1.0 / _vsh_src2.x");
        if (!snapshot || !mac_write || !ilu_read ||
            !(snapshot < mac_write && mac_write < ilu_read)) {
            fputs("FAIL: paired MAC/ILU source C was not snapshotted before destination writes\\n", stderr);
            fputs(hlsl, stderr);
            return 51;
        }
    }

    puts("PASS: NV2A MOV, ADD, constant, and paired MAC/ILU shader translation");
    return 0;
}