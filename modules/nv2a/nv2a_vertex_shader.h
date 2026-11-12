#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XboxNV2A_VERTEX_PROGRAM_INFO {
 uint32_t instruction_count;
 uint16_t input_mask;
 uint16_t output_mask;
 uint64_t constant_mask[3];
 uint32_t uses_relative_constants;
} XboxNV2A_VERTEX_PROGRAM_INFO;
/* Validate up to 136 raw 4-DWORD instructions and report referenced inputs,
 * outputs and physical constants. Returns zero only for a terminated program.
 * This is validation/metadata extraction, not GPU shader compilation. */
__declspec(dllexport) int __cdecl XboxNV2A_ValidateVertexProgram(
 const uint32_t *words,uint32_t instruction_count,
 XboxNV2A_VERTEX_PROGRAM_INFO *metadata);
#ifdef __cplusplus
}
#endif
