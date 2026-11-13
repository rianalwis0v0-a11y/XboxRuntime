#include <cstdint>
#include <cstdio>
#include "input.h"

static_assert(sizeof(XBOX_INPUT_GAMEPAD) == 18, "Original Xbox gamepad layout must be 18 bytes");
static_assert(sizeof(XBOX_INPUT_STATE) == 24, "Original Xbox input state must retain DWORD alignment");
static_assert(sizeof(XBOX_INPUT_VIBRATION) == 4, "Original Xbox vibration layout must be 4 bytes");

int main() {
    if (ModuleStart() != 0) return 1;
    XBOX_INPUT_STATE state{};
    if (XboxInput_GetState(4, &state) != 1167u) return 2;
    if (XboxInput_GetState(0, nullptr) != 1167u) return 3;
    const uint32_t mask = XboxInput_GetConnectedMask();
    if (mask & ~0x0Fu) return 4;

    // Exercise the translation deterministically without a physical controller.
    constexpr uint16_t buttons = 0xF3FF; // face buttons, Black/White, shoulders, d-pad, Start/Back/stick clicks
    if (XboxInput_TranslateHostState(0x12345678, buttons, 0x35, 0xE7,
                                     -32768, 1234, 32767, -2345, &state) != 0) return 5;
    if (state.dwPacketNumber != 0x12345678) return 6;
    if (state.Gamepad.wButtons != buttons) return 7;
    const uint8_t expected[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x35, 0xE7 };
    for (unsigned i = 0; i < 8; ++i)
        if (state.Gamepad.bAnalogButtons[i] != expected[i]) return 8;
    if (state.Gamepad.sThumbLX != -32768 || state.Gamepad.sThumbLY != 1234 ||
        state.Gamepad.sThumbRX != 32767 || state.Gamepad.sThumbRY != -2345) return 9;
    if (XboxInput_TranslateHostState(1, 0, 0, 0, 0, 0, 0, 0, nullptr) != 87u) return 10;

    // A disconnected/neutral sample must clear all previous button and axis data.
    if (XboxInput_TranslateHostState(9, 0, 0, 0, 0, 0, 0, 0, &state) != 0) return 11;
    if (state.dwPacketNumber != 9 || state.Gamepad.wButtons != 0) return 12;
    for (uint8_t value : state.Gamepad.bAnalogButtons) if (value != 0) return 13;
    if (state.Gamepad.sThumbLX || state.Gamepad.sThumbLY ||
        state.Gamepad.sThumbRX || state.Gamepad.sThumbRY) return 14;

    ModuleStop();
    std::puts("PASS: Xbox input ABI, host-state translation, analog triggers, buttons, sticks, and invalid arguments");
    return 0;
}
