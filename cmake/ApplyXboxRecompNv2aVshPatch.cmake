if(NOT WIN32)
    return()
endif()

set(_xbr_root "${CMAKE_CURRENT_LIST_DIR}/../third_party/xboxrecomp")
set(_vsh_c "${_xbr_root}/src/d3d/d3d8_vsh.c")
set(_vsh_h "${_xbr_root}/src/d3d/d3d8_vsh.h")
if(NOT EXISTS "${_vsh_c}" OR NOT EXISTS "${_vsh_h}")
    message(FATAL_ERROR "Pinned xboxrecomp D3D8 vertex-shader sources are missing")
endif()

function(_replace_range input_var start_marker end_marker replacement_var output_var)
    set(_text "${${input_var}}")
    string(FIND "${_text}" "${start_marker}" _start)
    string(FIND "${_text}" "${end_marker}" _end)
    if(_start LESS 0 OR _end LESS 0 OR _end LESS _start)
        message(FATAL_ERROR "Could not locate pinned xboxrecomp patch range: ${start_marker}")
    endif()
    string(SUBSTRING "${_text}" 0 ${_start} _prefix)
    string(SUBSTRING "${_text}" ${_end} -1 _suffix)
    set(${output_var} "${_prefix}${${replacement_var}}${_suffix}" PARENT_SCOPE)
endfunction()

file(READ "${_vsh_c}" _vsh_src)
string(FIND "${_vsh_src}" "XBOXRUNTIME_NV2A_VSH_LAYOUT_PATCH" _src_patched)
if(_src_patched LESS 0)
    set(_layout_note [=[
/* XBOXRUNTIME_NV2A_VSH_LAYOUT_PATCH
 * NV2A vertex microcode is four DWORDs. DWORD0 is unused; opcode and
 * shared indices live in DWORD1, source muxes/registers in DWORD2/3,
 * and destination masks/output routing/final flag in DWORD3.
 */

/* ================================================================
]=])
    _replace_range(_vsh_src "/* NV2A Instruction Bit Field Extraction" " * Module State" _layout_note _vsh_src)

    set(_parser_block [=[
/* Decode one NV2A source using the hardware DWORD layout.
 * mux: 1=temp, 2=input, 0/3=constant. */
static void parse_nv2a_source(const DWORD *insn, int which, int input_index,
                              int const_index, NV2AVshSrcOperand *src)
{
    uint32_t neg = 0, swz = 0, mux = 0, reg = 0;

    if (which == 0) {
        neg = (insn[1] >> 8) & 1u;
        swz = insn[1] & 0xFFu;
        mux = (insn[2] >> 26) & 3u;
        reg = (insn[2] >> 28) & 0xFu;
    } else if (which == 1) {
        neg = (insn[2] >> 25) & 1u;
        swz = (insn[2] >> 17) & 0xFFu;
        mux = (insn[2] >> 11) & 3u;
        reg = (insn[2] >> 13) & 0xFu;
    } else {
        neg = (insn[2] >> 10) & 1u;
        swz = (insn[2] >> 2) & 0xFFu;
        mux = (insn[3] >> 28) & 3u;
        reg = ((insn[2] & 3u) << 2) | ((insn[3] >> 30) & 3u);
    }

    src->negate = (uint8_t)neg;
    src->swizzle.x = (uint8_t)((swz >> 6) & 3u);
    src->swizzle.y = (uint8_t)((swz >> 4) & 3u);
    src->swizzle.z = (uint8_t)((swz >> 2) & 3u);
    src->swizzle.w = (uint8_t)(swz & 3u);
    src->rel_addr = (uint8_t)(((insn[3] >> 1) & 1u) && mux != 1u && mux != 2u);

    if (mux == 1u) {
        src->reg_type = NV2A_VSH_REG_TEMP;
        src->reg_index = (int)reg;
    } else if (mux == 2u) {
        src->reg_type = NV2A_VSH_REG_INPUT;
        src->reg_index = input_index;
    } else {
        src->reg_type = NV2A_VSH_REG_CONST;
        src->reg_index = const_index;
    }
}

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

static void track_vsh_input(NV2AVshProgram *program, const NV2AVshSrcOperand *src)
{
    if (src->reg_type == NV2A_VSH_REG_INPUT &&
        src->reg_index >= 0 && src->reg_index < NV2A_VS_MAX_INPUTS)
        program->inputs_read |= (uint16_t)(1u << src->reg_index);
}

void d3d8_vsh_parse(const DWORD *microcode, int num_insns, NV2AVshProgram *program)
{
    int i;
    if (!program) return;
    memset(program, 0, sizeof(*program));
    if (!microcode || num_insns <= 0) return;
    if (num_insns > NV2A_VS_MAX_INSTRUCTIONS)
        num_insns = NV2A_VS_MAX_INSTRUCTIONS;

    for (i = 0; i < num_insns; ++i) {
        const DWORD *insn = &microcode[i * 4];
        NV2AVshInstruction *inst = &program->insns[i];
        const uint32_t mac = (insn[1] >> 21) & 0xFu;
        const uint32_t ilu = (insn[1] >> 25) & 0x7u;
        const uint32_t mac_temp_mask = (insn[3] >> 24) & 0xFu;
        const uint32_t ilu_temp_mask = (insn[3] >> 16) & 0xFu;
        const uint32_t temp_dest = (insn[3] >> 20) & 0xFu;
        const uint32_t output_mask = (insn[3] >> 12) & 0xFu;
        const uint32_t output_to_o = (insn[3] >> 11) & 1u;
        const uint32_t output_from_ilu = (insn[3] >> 2) & 1u;
        const uint32_t output_address = (insn[3] >> 3) & 0xFFu;
        NV2AVshOutputReg output_reg = NV2A_VSH_OUT_NONE;

        inst->mac_op = (NV2AVshMacOp)mac;
        inst->ilu_op = (NV2AVshIluOp)ilu;
        inst->const_index = (int)((insn[1] >> 13) & 0xFFu);
        inst->input_index = (int)((insn[1] >> 9) & 0xFu);
        if (inst->const_index >= NV2A_VS_MAX_CONSTANTS)
            inst->const_index = 0;

        parse_nv2a_source(insn, 0, inst->input_index, inst->const_index, &inst->mac_src[0]);
        parse_nv2a_source(insn, 1, inst->input_index, inst->const_index, &inst->mac_src[1]);
        parse_nv2a_source(insn, 2, inst->input_index, inst->const_index, &inst->mac_src[2]);
        inst->ilu_src = inst->mac_src[2];

        if (output_to_o && output_mask)
            output_reg = decode_output_mux(output_address);

        inst->mac_dst.temp_reg = (mac && mac_temp_mask) ? (int)temp_dest : -1;
        inst->mac_dst.write_mask = (uint8_t)(mac ? mac_temp_mask : 0u);
        inst->mac_dst.output_reg = (mac && !output_from_ilu) ? output_reg : NV2A_VSH_OUT_NONE;
        inst->mac_dst.output_write_mask = (inst->mac_dst.output_reg != NV2A_VSH_OUT_NONE)
            ? (uint8_t)output_mask : 0u;

        inst->ilu_dst.temp_reg = (ilu && ilu_temp_mask)
            ? (mac ? 1 : (int)temp_dest) : -1;
        inst->ilu_dst.write_mask = (uint8_t)(ilu ? ilu_temp_mask : 0u);
        inst->ilu_dst.output_reg = (ilu && output_from_ilu) ? output_reg : NV2A_VSH_OUT_NONE;
        inst->ilu_dst.output_write_mask = (inst->ilu_dst.output_reg != NV2A_VSH_OUT_NONE)
            ? (uint8_t)output_mask : 0u;

        inst->is_final = (int)(insn[3] & 1u);

        if (mac) {
            switch (inst->mac_op) {
            case NV2A_VSH_MAC_MOV:
            case NV2A_VSH_MAC_ARL:
                track_vsh_input(program, &inst->mac_src[0]);
                break;
            case NV2A_VSH_MAC_MUL:
            case NV2A_VSH_MAC_DP3:
            case NV2A_VSH_MAC_DPH:
            case NV2A_VSH_MAC_DP4:
            case NV2A_VSH_MAC_DST:
            case NV2A_VSH_MAC_MIN:
            case NV2A_VSH_MAC_MAX:
            case NV2A_VSH_MAC_SLT:
            case NV2A_VSH_MAC_SGE:
                track_vsh_input(program, &inst->mac_src[0]);
                track_vsh_input(program, &inst->mac_src[1]);
                break;
            case NV2A_VSH_MAC_ADD:
                track_vsh_input(program, &inst->mac_src[0]);
                track_vsh_input(program, &inst->mac_src[2]);
                break;
            case NV2A_VSH_MAC_MAD:
                track_vsh_input(program, &inst->mac_src[0]);
                track_vsh_input(program, &inst->mac_src[1]);
                track_vsh_input(program, &inst->mac_src[2]);
                break;
            default:
                track_vsh_input(program, &inst->mac_src[0]);
                track_vsh_input(program, &inst->mac_src[1]);
                track_vsh_input(program, &inst->mac_src[2]);
                break;
            }
        }
        if (ilu)
            track_vsh_input(program, &inst->ilu_src);

        program->length = i + 1;
        if (inst->is_final)
            break;
    }
}

]=])
    _replace_range(_vsh_src "static void parse_source(" " * HLSL Code Generator" _parser_block _vsh_src)

    set(_dest_block [=[
static void emit_dest_assign(StrBuf *sb, const NV2AVshDstOperand *dst,
                              const char *rhs)
{
    if (dst->temp_reg >= 0 && dst->write_mask != 0) {
        if (dst->temp_reg == 12)
            sb_append(sb, "    R12");
        else
            sb_append(sb, "    R%d", dst->temp_reg);
        emit_write_mask(sb, dst->write_mask);
        sb_append(sb, " = (%s)", rhs);
        emit_write_mask(sb, dst->write_mask);
        sb_append(sb, ";\n");
    }

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
}

]=])
    _replace_range(_vsh_src "static void emit_dest_assign(" " * Emit the HLSL for one MAC operation." _dest_block _vsh_src)
    file(WRITE "${_vsh_c}" "${_vsh_src}")
endif()

file(READ "${_vsh_h}" _vsh_hdr)
string(FIND "${_vsh_hdr}" "XBOXRUNTIME_NV2A_VSH_OUTPUT_MASK_PATCH" _hdr_patched)
if(_hdr_patched LESS 0)
    string(FIND "${_vsh_hdr}" "typedef struct NV2AVshDstOperand {" _dst_start)
    string(FIND "${_vsh_hdr}" "} NV2AVshDstOperand;" _dst_end)
    if(_dst_start LESS 0 OR _dst_end LESS _dst_start)
        message(FATAL_ERROR "Could not locate NV2AVshDstOperand in pinned xboxrecomp header")
    endif()
    string(LENGTH "} NV2AVshDstOperand;" _dst_end_len)
    math(EXPR _dst_after_end "${_dst_end} + ${_dst_end_len}")
    string(SUBSTRING "${_vsh_hdr}" 0 ${_dst_start} _hdr_prefix)
    string(SUBSTRING "${_vsh_hdr}" ${_dst_after_end} -1 _hdr_suffix)
    set(_new_dst [=[
typedef struct NV2AVshDstOperand {
    int               temp_reg;    /* Temp register index (0-12), or -1 if none */
    NV2AVshOutputReg  output_reg;  /* Output register, or NV2A_VSH_OUT_NONE */
    uint8_t           write_mask;  /* MAC/ILU temp-register write mask */
    uint8_t           output_write_mask; /* Output-register write mask */
} NV2AVshDstOperand; /* XBOXRUNTIME_NV2A_VSH_OUTPUT_MASK_PATCH */
]=])
    set(_vsh_hdr "${_hdr_prefix}${_new_dst}${_hdr_suffix}")
    file(WRITE "${_vsh_h}" "${_vsh_hdr}")
endif()

message(STATUS "Applied XboxRuntime NV2A vertex-shader microcode layout patch to pinned xboxrecomp")