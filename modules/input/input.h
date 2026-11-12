#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Original Xbox XINPUT_GAMEPAD layout (18 bytes), not the Windows layout.
typedef struct XBOX_INPUT_GAMEPAD {
    uint16_t wButtons;
    uint8_t bAnalogButtons[8]; // A, B, X, Y, Black, White, LT, RT
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} XBOX_INPUT_GAMEPAD;

typedef struct XBOX_INPUT_STATE {
    uint32_t dwPacketNumber;
    XBOX_INPUT_GAMEPAD Gamepad;
} XBOX_INPUT_STATE;

typedef struct XBOX_INPUT_VIBRATION {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
} XBOX_INPUT_VIBRATION;

__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);
__declspec(dllexport) uint32_t __cdecl XboxInput_GetConnectedMask(void);
__declspec(dllexport) uint32_t __cdecl XboxInput_GetState(uint32_t userIndex, XBOX_INPUT_STATE* state);
__declspec(dllexport) uint32_t __cdecl XboxInput_SetState(uint32_t userIndex, const XBOX_INPUT_VIBRATION* vibration);

#ifdef __cplusplus
}
#endif
