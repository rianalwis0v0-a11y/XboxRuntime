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

    if (strstr(label, "MOV input-to-oPos") &&
        (program.length != 1 ||
         program.insns[0].mac_op != NV2A_VSH_MAC_MOV ||
         program.insns[0].mac_src[0].reg_type != NV2A_VSH_REG_INPUT ||
         program.insns[0].mac_src[0].reg_index != 0 ||
         program.inputs_read != 1u ||
         program.insns[0].mac_dst.output_reg != NV2A_VSH_OUT_POS ||
         program.insns[0].mac_dst.output_write_mask != 0xFu)) {
        fprintf(stderr, "FAIL: %s microcode decode did not preserve the real NV2A fields\\n", label);
        return 5;
    }
    if (strstr(label, "ADD input-to-oPos") &&
        (program.length != 1 ||
         program.insns[0].mac_op != NV2A_VSH_MAC_ADD ||
         program.insns[0].mac_src[0].reg_type != NV2A_VSH_REG_INPUT ||
         program.insns[0].mac_src[2].reg_type != NV2A_VSH_REG_INPUT ||
         program.inputs_read != (1u << 5) ||
         program.insns[0].mac_dst.output_reg != NV2A_VSH_OUT_POS ||
         program.insns[0].mac_dst.output_write_mask != 0xFu)) {
        fprintf(stderr, "FAIL: %s ADD A+C decode is incorrect\\n", label);
        return 7;
    }
    if (strstr(label, "MAD A+B+C") &&
        (program.insns[0].mac_op != NV2A_VSH_MAC_MAD ||
         program.insns[0].mac_src[0].reg_type != NV2A_VSH_REG_TEMP ||
         program.insns[0].mac_src[1].reg_type != NV2A_VSH_REG_INPUT ||
         program.insns[0].mac_src[2].reg_type != NV2A_VSH_REG_CONST ||
         program.inputs_read != (1u << 2) ||
         program.insns[0].mac_dst.output_reg != NV2A_VSH_OUT_POS ||
         program.insns[0].mac_dst.output_write_mask != 0xFu)) {
        fprintf(stderr, "FAIL: %s MAD A+B+C decode is incorrect\\n", label);
        return 8;
    }
    if (strstr(label, "ILU LIT C") &&
        (program.insns[0].ilu_op != NV2A_VSH_ILU_LIT ||
         program.insns[0].ilu_src.reg_type != NV2A_VSH_REG_CONST ||
         program.insns[0].ilu_src.reg_index != 3 ||
         program.insns[0].ilu_dst.output_reg != NV2A_VSH_OUT_POS ||
         program.insns[0].ilu_dst.output_write_mask != 0xFu)) {
        fprintf(stderr, "FAIL: %s ILU C/output decode is incorrect\\n", label);
        return 9;
    }
    if (strstr(label, "constant-to-oPos") &&
        (program.insns[0].mac_op != NV2A_VSH_MAC_MOV ||
         program.insns[0].mac_src[0].reg_type != NV2A_VSH_REG_CONST ||
         program.insns[0].mac_src[0].reg_index != 5)) {
        fprintf(stderr, "FAIL: %s constant source decode is incorrect\\n", label);
        return 6;
    }

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

    /* MAC ADD v5 + v5 -> oPos exercises a two-source arithmetic operation. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (3u << 21) | (5u << 9);
    instruction[2] = (2u << 26);
    instruction[3] = (2u << 28) | (0xFu << 12) | (1u << 11) | 1u;
    if (compile_program(instruction, 1u, "NV2A ADD input-to-oPos") != 0)
        return 3;

    /* MAC MAD R0 * v2 + c9 -> oPos exercises all three source fields. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (4u << 21) | (9u << 13) | (2u << 9);
    instruction[2] = (1u << 26) | (2u << 11);
    instruction[3] = (3u << 28) | (0xFu << 12) | (1u << 11) | 1u;
    if (compile_program(instruction, 1u, "NV2A MAD A+B+C") != 0)
        return 4;

    /* ILU LIT c3 -> oPos exercises the paired ILU output mux. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (7u << 25) | (3u << 13);
    instruction[3] = (3u << 28) | (0xFu << 12) | (1u << 11) |
                     (1u << 2) | 1u;
    if (compile_program(instruction, 1u, "NV2A ILU LIT C") != 0)
        return 5;

    /* MAC MOV c[5] -> oPos exercises constant-register source emission. */
    memset(instruction, 0, sizeof(instruction));
    instruction[1] = (1u << 21) | (5u << 13) | 0x1Bu;
    instruction[2] = 3u << 26;
    instruction[3] = (0xFu << 12) | (1u << 11) | 1u;
    if (compile_program(instruction, 1u, "NV2A MOV constant-to-oPos") != 0)
        return 4;

    puts("PASS: NV2A NOP, MOV, ADD, MAD, ILU LIT, and constant shader translation");
    return 0;
}
