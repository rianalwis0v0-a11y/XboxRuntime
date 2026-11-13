#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host audio bridge. Samples are signed 16-bit little-endian PCM, interleaved.
__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);
__declspec(dllexport) uint32_t __cdecl XboxAudio_Open(uint32_t sampleRate, uint16_t channels);
__declspec(dllexport) uint32_t __cdecl XboxAudio_SubmitPcm16(const int16_t* samples, uint32_t frameCount);
__declspec(dllexport) uint32_t __cdecl XboxAudio_SetVolume(uint16_t left, uint16_t right);
__declspec(dllexport) void __cdecl XboxAudio_Close(void);

#ifdef __cplusplus
}
#endif
