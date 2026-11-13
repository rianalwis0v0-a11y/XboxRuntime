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

// Converts a Windows-XInput-shaped host sample into the original Xbox ABI.
// This is a host-backend helper, not an Xbox guest import/export entry point.
__declspec(dllexport) uint32_t __cdecl XboxInput_TranslateHostState(
    uint32_t packetNumber, uint16_t hostButtons,
    uint8_t leftTrigger, uint8_t rightTrigger,
    int16_t thumbLX, int16_t thumbLY, int16_t thumbRX, int16_t thumbRY,
    XBOX_INPUT_STATE* output);

#ifdef __cplusplus
}
#endif
