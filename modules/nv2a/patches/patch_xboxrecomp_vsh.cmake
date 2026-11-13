# Apply a narrow compatibility patch to the pinned upstream D3D8 VSH parser.
# The upstream parser's field offsets predate the real NV2A 4-DWORD layout.
if(NOT WIN32)
    return()
endif()

set(_d3d_dir "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/xboxrecomp/src/d3d")
set(_vsh_c "${_d3d_dir}/d3d8_vsh.c")
set(_vsh_h "${_d3d_dir}/d3d8_vsh.h")
if(NOT EXISTS "${_vsh_c}" OR NOT EXISTS "${_vsh_h}")
    message(FATAL_ERROR "Pinned xboxrecomp D3D8 VSH sources are missing")
endif()

file(READ "${_vsh_h}" _header)
if(NOT _header MATCHES "output_write_mask")
    string(REPLACE
        "uint8_t           write_mask;  /* Bitmask: bit3=x, bit2=y, bit1=z, bit0=w */"
        "uint8_t           write_mask;  /* Temporary-register write mask */\n    uint8_t           output_write_mask; /* Separate NV2A output-register mask */"
        _header "${_header}")
    if(NOT _header MATCHES "output_write_mask")
        message(FATAL_ERROR "Could not patch NV2A VSH destination structure")
    endif()
    file(WRITE "${_vsh_h}" "${_header}")
endif()

file(READ "${_vsh_c}" _source)
if(NOT _source MATCHES "XBOXRUNTIME_NV2A_VSH_PATCH")
    set(_start_marker "static void parse_source(")
    set(_end_marker "/* ================================================================\n * HLSL Code Generator")
    string(FIND "${_source}" "${_start_marker}" _start)
    string(FIND "${_source}" "${_end_marker}" _end)
    if(_start LESS 0 OR _end LESS 0 OR _end LESS _start)
        message(FATAL_ERROR "Could not locate upstream NV2A VSH parser for patching")
    endif()
    string(SUBSTRING "${_source}" 0 ${_start} _before)
    string(LENGTH "${_source}" _source_len)
    math(EXPR _after_len "${_source_len} - ${_end}")
    string(SUBSTRING "${_source}" ${_end} ${_after_len} _after)

    set(_replacement [=[
/* XBOXRUNTIME_NV2A_VSH_PATCH: decode the actual four-DWORD NV2A instruction. */
static NV2AVshOutputReg decode_output_mux(uint32_t mux_val)
{
    switch (mux_val & 0xFu) {
    case 0: return NV2A_VSH_OUT_POS;
    case 3: return NV2A_VSH_OUT_D0;
    case 4: return NV2A_VSH_OUT_D1;
    case 5: return NV2A_VSH_OUT_FOG;
    case 6: return NV2A_VSH_OUT_PTS;
    case 7: return NV2A_VSH_OUT_B0;
    case 8: return NV2A_VSH_OUT_B1;
    case 9: return NV2A_VSH_OUT_T0;
    case 10: return NV2A_VSH_OUT_T1;
    case 11: return NV2A_VSH_OUT_T2;
    case 12: return NV2A_VSH_OUT_T3;
    default: return NV2A_VSH_OUT_NONE;
    }
}

static void nv2a_decode_source(const DWORD *w, int slot,
                               int input_index, int const_index,
                               NV2AVshSrcOperand *src)
{
    unsigned mux = 0, reg = 0, neg = 0;
    memset(src, 0, sizeof(*src));
    if (slot == 0) {
        mux = (w[2] >> 26) & 3u;
        reg = (w[2] >> 28) & 15u;
        neg = (w[1] >> 8) & 1u;
        src->swizzle.x = (uint8_t)((w[1] >> 6) & 3u);
        src->swizzle.y = (uint8_t)((w[1] >> 4) & 3u);
        src->swizzle.z = (uint8_t)((w[1] >> 2) & 3u);
        src->swizzle.w = (uint8_t)(w[1] & 3u);
    } else if (slot == 1) {
        mux = (w[2] >> 11) & 3u;
        reg = (w[2] >> 13) & 15u;
        neg = (w[2] >> 25) & 1u;
        src->swizzle.x = (uint8_t)((w[2] >> 23) & 3u);
        src->swizzle.y = (uint8_t)((w[2] >> 21) & 3u);
        src->swizzle.z = (uint8_t)((w[2] >> 19) & 3u);
        src->swizzle.w = (uint8_t)((w[2] >> 17) & 3u);
    } else {
        mux = (w[3] >> 28) & 3u;
        reg = (((w[2] & 3u) << 2) | ((w[3] >> 30) & 3u));
        neg = (w[2] >> 10) & 1u;
        src->swizzle.x = (uint8_t)((w[2] >> 8) & 3u);
        src->swizzle.y = (uint8_t)((w[2] >> 6) & 3u);
        src->swizzle.z = (uint8_t)((w[2] >> 4) & 3u);
        src->swizzle.w = (uint8_t)((w[2] >> 2) & 3u);
    }
    src->negate = (int)neg;
    switch (mux) {
    case 1: /* temporary R0-R12 */
        src->reg_type = NV2A_VSH_REG_TEMP;
        src->reg_index = reg <= 12u ? (int)reg : 0;
        break;
    case 2: /* vertex input v0-v15 */
        src->reg_type = NV2A_VSH_REG_INPUT;
        src->reg_index = input_index;
        break;
    case 3: /* constant c0-c191 */
        src->reg_type = NV2A_VSH_REG_CONST;
        src->reg_index = const_index;
        src->rel_addr = (w[3] & 2u) ? 1 : 0;
        break;
    default: /* Reserved mux; the XboxRuntime validator rejects it. */
        src->reg_type = NV2A_VSH_REG_TEMP;
        src->reg_index = 0;
        break;
    }
}

void d3d8_vsh_parse(const DWORD *microcode, int num_insns,
                    NV2AVshProgram *program)
{
    int i;
    memset(program, 0, sizeof(*program));
    if (!microcode || num_insns <= 0)
        return;
    if (num_insns > NV2A_VS_MAX_INSTRUCTIONS)
        num_insns = NV2A_VS_MAX_INSTRUCTIONS;

    for (i = 0; i < num_insns; ++i) {
        const DWORD *w = &microcode[i * 4];
        NV2AVshInstruction *inst = &program->insns[i];
        const unsigned mac = (w[1] >> 21) & 15u;
        const unsigned ilu = (w[1] >> 25) & 7u;
        const unsigned input_index = (w[1] >> 9) & 15u;
        const unsigned const_index = (w[1] >> 13) & 255u;
        const unsigned temp_index = (w[3] >> 20) & 15u;
        const unsigned mac_mask = (w[3] >> 24) & 15u;
        const unsigned ilu_mask = (w[3] >> 16) & 15u;
        const unsigned output_mask = (w[3] >> 12) & 15u;
        const unsigned output_bank = (w[3] >> 11) & 1u;
        const unsigned output_address = (w[3] >> 3) & 255u;
        const unsigned output_mux = (w[3] >> 2) & 1u;

        inst->mac_op = (NV2AVshMacOp)mac;
        inst->ilu_op = (NV2AVshIluOp)ilu;
        inst->const_index = const_index < NV2A_VS_MAX_CONSTANTS ? (int)const_index : 0;
        inst->input_index = (int)input_index;
        inst->is_final = (w[3] & 1u) ? 1 : 0;

        if (mac != NV2A_VSH_MAC_NOP) {
            nv2a_decode_source(w, 0, (int)input_index, (int)const_index,
                               &inst->mac_src[0]);
            if (mac == NV2A_VSH_MAC_MUL || mac == NV2A_VSH_MAC_MAD ||
                (mac >= NV2A_VSH_MAC_DP3 && mac <= NV2A_VSH_MAC_SGE)) {
                nv2a_decode_source(w, 1, (int)input_index, (int)const_index,
                                   &inst->mac_src[1]);
            }
            if (mac == NV2A_VSH_MAC_ADD || mac == NV2A_VSH_MAC_MAD) {
                nv2a_decode_source(w, 2, (int)input_index, (int)const_index,
                                   &inst->mac_src[2]);
            }
        }
        if (ilu != NV2A_VSH_ILU_NOP) {
            nv2a_decode_source(w, 2, (int)input_index, (int)const_index,
                               &inst->ilu_src);
        } else {
            inst->ilu_src = inst->mac_src[2];
        }

        {
            int s;
            for (s = 0; s < 3; ++s) {
                if (inst->mac_src[s].reg_type == NV2A_VSH_REG_INPUT &&
                    (mac != NV2A_VSH_MAC_NOP)) {
                    program->inputs_read |= (uint16_t)(1u << inst->mac_src[s].reg_index);
                }
            }
            if (ilu != NV2A_VSH_ILU_NOP &&
                inst->ilu_src.reg_type == NV2A_VSH_REG_INPUT) {
                program->inputs_read |= (uint16_t)(1u << inst->ilu_src.reg_index);
            }
        }

        inst->mac_dst.temp_reg = (mac != NV2A_VSH_MAC_NOP && mac_mask) ?
                                 (int)temp_index : -1;
        inst->mac_dst.write_mask = (uint8_t)mac_mask;
        inst->mac_dst.output_reg = NV2A_VSH_OUT_NONE;
        inst->mac_dst.output_write_mask = 0;

        inst->ilu_dst.temp_reg = (ilu != NV2A_VSH_ILU_NOP && ilu_mask) ?
                                 (mac != NV2A_VSH_MAC_NOP ? 1 : (int)temp_index) : -1;
        inst->ilu_dst.write_mask = (uint8_t)ilu_mask;
        inst->ilu_dst.output_reg = NV2A_VSH_OUT_NONE;
        inst->ilu_dst.output_write_mask = 0;

        /* Paired ILU owns R1; a paired MAC write to R1 is suppressed. */
        if (mac != NV2A_VSH_MAC_NOP && ilu != NV2A_VSH_ILU_NOP &&
            temp_index == 1u) {
            inst->mac_dst.temp_reg = -1;
            inst->mac_dst.write_mask = 0;
        }

        /* The output mux selects which execution unit writes the output bank.
         * Writable-constant (bank 0) side effects are intentionally not emitted. */
        if (output_bank && output_mask) {
            const NV2AVshOutputReg output = decode_output_mux(output_address);
            if (output_mux == 0u && mac != NV2A_VSH_MAC_NOP) {
                inst->mac_dst.output_reg = output;
                inst->mac_dst.output_write_mask = (uint8_t)output_mask;
            } else if (output_mux == 1u && ilu != NV2A_VSH_ILU_NOP) {
                inst->ilu_dst.output_reg = output;
                inst->ilu_dst.output_write_mask = (uint8_t)output_mask;
            }
        }

        program->length = i + 1;
        if (inst->is_final)
            break;
    }
}

]=])

    set(_patched "${_before}${_replacement}${_after}")
    file(WRITE "${_vsh_c}" "${_patched}")
endif()

file(READ "${_vsh_c}" _source)
if(NOT _source MATCHES "dst->output_write_mask")
    set(_old_output [=[
    /* Write to output register if specified */
    if (dst->output_reg != NV2A_VSH_OUT_NONE && dst->write_mask != 0) {
        const char *name = output_reg_name(dst->output_reg);
        if (name) {
            sb_append(sb, "    %s", name);
            emit_write_mask(sb, dst->write_mask);
            sb_append(sb, " = (%s)", rhs);
            emit_write_mask(sb, dst->write_mask);
            sb_append(sb, ";\n");
        }
    }
]=])
    set(_new_output [=[
    /* Output-register mask is independent of the temporary-register mask. */
    if (dst->output_reg != NV2A_VSH_OUT_NONE && dst->output_write_mask != 0) {
        const char *name = output_reg_name(dst->output_reg);
        if (name) {
            sb_append(sb, "    %s", name);
            emit_write_mask(sb, dst->output_write_mask);
            sb_append(sb, " = (%s)", rhs);
            emit_write_mask(sb, dst->output_write_mask);
            sb_append(sb, ";\n");
        }
    }
]=])
    string(REPLACE "${_old_output}" "${_new_output}" _source "${_source}")
    if(NOT _source MATCHES "dst->output_write_mask")
        message(FATAL_ERROR "Could not patch NV2A output write mask handling")
    endif()
    file(WRITE "${_vsh_c}" "${_source}")
endif()
