#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI DirectSoundCreate(LPCGUID device, void** sound, void* outer);
HRESULT WINAPI DirectSoundCreate8(LPCGUID device, void** sound, void* outer);
#ifdef __cplusplus
}
#endif
