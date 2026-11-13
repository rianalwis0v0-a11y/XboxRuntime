#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "input.h"

namespace {
using DWORD = unsigned long;
using WORD = unsigned short;
using BYTE = unsigned char;
using SHORT = short;

struct WIN_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct WIN_STATE { DWORD dwPacketNumber; WIN_GAMEPAD Gamepad; };
struct WIN_VIBRATION { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; };
using GetStateFn = DWORD (WINAPI*)(DWORD, WIN_STATE*);
using SetStateFn = DWORD (WINAPI*)(DWORD, WIN_VIBRATION*);

constexpr DWORD ERROR_SUCCESS_VALUE = 0;
constexpr DWORD ERROR_DEVICE_NOT_CONNECTED_VALUE = 1167;
constexpr DWORD ERROR_INVALID_PARAMETER_VALUE = 87;

HMODULE g_xinput = nullptr;
GetStateFn g_getState = nullptr;
SetStateFn g_setState = nullptr;
bool g_started = false;

void Log(const wchar_t* message) { OutputDebugStringW(message); }

bool LoadXInput() {
    const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (const wchar_t* name : names) {
        HMODULE module = LoadLibraryW(name);
        if (!module) continue;
        auto getState = reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"));
        auto setState = reinterpret_cast<SetStateFn>(GetProcAddress(module, "XInputSetState"));
        if (getState) {
            g_xinput = module;
            g_getState = getState;
            g_setState = setState;
            Log(L"[XboxRuntime:Input] Loaded Windows XInput provider.\n");
            return true;
        }
        FreeLibrary(module);
    }
    Log(L"[XboxRuntime:Input] No Windows XInput provider found; input stays disconnected.\n");
    return false;
}
}

extern "C" uint32_t __cdecl XboxInput_TranslateHostState(
    uint32_t packetNumber, uint16_t hostButtons,
    uint8_t leftTrigger, uint8_t rightTrigger,
    int16_t thumbLX, int16_t thumbLY, int16_t thumbRX, int16_t thumbRY,
    XBOX_INPUT_STATE* output) {
    if (!output) return ERROR_INVALID_PARAMETER_VALUE;
    std::memset(output, 0, sizeof(*output));
    output->dwPacketNumber = packetNumber;

    // Windows XInput digital button masks share the original Xbox bit values.
    output->Gamepad.wButtons = hostButtons;
    output->Gamepad.bAnalogButtons[0] = (hostButtons & 0x1000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[1] = (hostButtons & 0x2000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[2] = (hostButtons & 0x4000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[3] = (hostButtons & 0x8000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[4] = (hostButtons & 0x0100) ? 0xFF : 0; // Black
    output->Gamepad.bAnalogButtons[5] = (hostButtons & 0x0200) ? 0xFF : 0; // White
    output->Gamepad.bAnalogButtons[6] = leftTrigger;
    output->Gamepad.bAnalogButtons[7] = rightTrigger;
    output->Gamepad.sThumbLX = thumbLX;
    output->Gamepad.sThumbLY = thumbLY;
    output->Gamepad.sThumbRX = thumbRX;
    output->Gamepad.sThumbRY = thumbRY;
    return ERROR_SUCCESS_VALUE;
}

extern "C" int __cdecl ModuleStart(void) {
    if (g_started) return 0;
    LoadXInput(); // Missing host XInput is non-fatal; games can still boot.
    g_started = true;
    return 0;
}

extern "C" void __cdecl ModuleStop(void) {
    g_started = false;
    g_getState = nullptr;
    g_setState = nullptr;
    if (g_xinput) FreeLibrary(g_xinput);
    g_xinput = nullptr;
}

extern "C" uint32_t __cdecl XboxInput_GetConnectedMask(void) {
    if (!g_started || !g_getState) return 0;
    uint32_t mask = 0;
    for (DWORD i = 0; i < 4; ++i) {
        WIN_STATE state{};
        if (g_getState(i, &state) == ERROR_SUCCESS_VALUE) mask |= (1u << i);
    }
    return mask;
}

extern "C" uint32_t __cdecl XboxInput_GetState(uint32_t userIndex, XBOX_INPUT_STATE* output) {
    if (!g_started || !output || userIndex >= 4) return ERROR_DEVICE_NOT_CONNECTED_VALUE;
    std::memset(output, 0, sizeof(*output));
    if (!g_getState) return ERROR_DEVICE_NOT_CONNECTED_VALUE;

    WIN_STATE host{};
    const DWORD result = g_getState(userIndex, &host);
    if (result != ERROR_SUCCESS_VALUE) return result;

    return XboxInput_TranslateHostState(
        static_cast<uint32_t>(host.dwPacketNumber), host.Gamepad.wButtons,
        host.Gamepad.bLeftTrigger, host.Gamepad.bRightTrigger,
        host.Gamepad.sThumbLX, host.Gamepad.sThumbLY,
        host.Gamepad.sThumbRX, host.Gamepad.sThumbRY, output);
}

extern "C" uint32_t __cdecl XboxInput_SetState(uint32_t userIndex, const XBOX_INPUT_VIBRATION* vibration) {
    if (!g_started || !vibration || userIndex >= 4 || !g_setState)
        return ERROR_DEVICE_NOT_CONNECTED_VALUE;
    WIN_VIBRATION host{ vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed };
    return g_setState(userIndex, &host);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
