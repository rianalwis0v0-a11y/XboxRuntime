#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nv2a_vsh_interp.h"

static int close_float(float a, float b)
{
    const float d = a - b;
    return d > -0.0001f && d < 0.0001f;
}

int main(void)
{
    float input[NV2A_VSH_INPUTS][4] = {{0}};
    Nv2aVshOutput output;
    uint32_t instruction[4] = {0, 0, 0, 0};

    /* MOV v0 -> oPos, all four components, and terminate the program. */
    input[0][0] = 2.0f; input[0][1] = 3.0f;
    input[0][2] = 4.0f; input[0][3] = 1.0f;
    instruction[1] = (1u << 21) | 0x1Bu;
    instruction[2] = 2u << 26;
    instruction[3] = (0xFu << 12) | (1u << 11) | 1u;
    nv2a_vsh_set_load_slot(0);
    for (unsigned i = 0; i < 4; ++i) nv2a_vsh_program_word(instruction[i]);
    nv2a_vsh_set_start_slot(0);
    if (!nv2a_vsh_run((const float (*)[4])input, &output) ||
        !close_float(output.pos[0], 2.0f) ||
        !close_float(output.pos[1], 3.0f) ||
        !close_float(output.pos[2], 4.0f) ||
        !close_float(output.pos[3], 1.0f)) {
        fputs("FAIL: NV2A MOV input-to-oPos execution\n", stderr);
        return 1;
    }

    /* MOV c[5] -> oPos verifies the constant upload port and register file. */
    {
        const float constant[4] = { 5.0f, 6.0f, 7.0f, 2.0f };
        uint32_t words[4];
        memcpy(words, constant, sizeof(words));
        nv2a_vsh_set_constant_load(5);
        for (unsigned i = 0; i < 4; ++i) nv2a_vsh_constant_word(words[i]);
        memset(instruction, 0, sizeof(instruction));
        instruction[1] = (1u << 21) | (5u << 13) | 0x1Bu;
        instruction[2] = 3u << 26;
        instruction[3] = (0xFu << 12) | (1u << 11) | 1u;
        nv2a_vsh_set_load_slot(1);
        for (unsigned i = 0; i < 4; ++i) nv2a_vsh_program_word(instruction[i]);
        nv2a_vsh_set_start_slot(1);
        if (!nv2a_vsh_run((const float (*)[4])input, &output) ||
            !close_float(output.pos[0], 5.0f) ||
            !close_float(output.pos[1], 6.0f) ||
            !close_float(output.pos[2], 7.0f) ||
            !close_float(output.pos[3], 2.0f)) {
            fputs("FAIL: NV2A constant-to-oPos execution\n", stderr);
            return 2;
        }
    }

    /* A program with no FINAL marker must fail closed. */
    instruction[3] &= ~1u;
    nv2a_vsh_set_instruction(2, instruction);
    nv2a_vsh_set_start_slot(2);
    if (nv2a_vsh_run((const float (*)[4])input, &output)) {
        fputs("FAIL: unterminated NV2A program was executed\n", stderr);
        return 3;
    }

    puts("PASS: NV2A vertex interpreter MOV, constants, and FINAL handling");
    return 0;
}
