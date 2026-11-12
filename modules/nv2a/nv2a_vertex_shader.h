/* XboxRuntime NV2A shader translation API. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Translate up to 136 raw four-DWORD instructions (without pushbuffer headers)
 * into HLSL. Returns 0 on success; nonzero on invalid/unsupported microcode or
 * insufficient output storage. The output is cleared on translation failure.
 * This is a source-generation front-end, not yet a D3D shader binding path. */
__declspec(dllexport) int __cdecl XboxNV2A_TranslateVertexProgram(
 const uint32_t *words,uint32_t instruction_count,char *hlsl,
 uint32_t hlsl_capacity,uint32_t *bytes_written);
#ifdef __cplusplus
}
#endif
