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
constexpr WORD XINPUT_DPAD_UP = 0x0001;
constexpr WORD XINPUT_DPAD_DOWN = 0x0002;
constexpr WORD XINPUT_DPAD_LEFT = 0x0004;
constexpr WORD XINPUT_DPAD_RIGHT = 0x0008;
constexpr WORD XINPUT_START = 0x0010;
constexpr WORD XINPUT_BACK = 0x0020;
constexpr WORD XINPUT_LTHUMB = 0x0040;
constexpr WORD XINPUT_RTHUMB = 0x0080;
constexpr WORD XINPUT_LSHOULDER = 0x0100;
constexpr WORD XINPUT_RSHOULDER = 0x0200;
constexpr WORD XINPUT_A = 0x1000;
constexpr WORD XINPUT_B = 0x2000;
constexpr WORD XINPUT_X = 0x4000;
constexpr WORD XINPUT_Y = 0x8000;

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

    output->dwPacketNumber = static_cast<uint32_t>(host.dwPacketNumber);
    const WORD b = host.Gamepad.wButtons;
    WORD mapped = 0;
    if (b & 0x0001) mapped |= XINPUT_DPAD_UP;
    if (b & 0x0002) mapped |= XINPUT_DPAD_DOWN;
    if (b & 0x0004) mapped |= XINPUT_DPAD_LEFT;
    if (b & 0x0008) mapped |= XINPUT_DPAD_RIGHT;
    if (b & 0x0010) mapped |= XINPUT_START;
    if (b & 0x0020) mapped |= XINPUT_BACK;
    if (b & 0x0040) mapped |= XINPUT_LTHUMB;
    if (b & 0x0080) mapped |= XINPUT_RTHUMB;
    if (b & 0x0100) mapped |= XINPUT_LSHOULDER; // Xbox Black
    if (b & 0x0200) mapped |= XINPUT_RSHOULDER; // Xbox White
    if (b & 0x1000) mapped |= XINPUT_A;
    if (b & 0x2000) mapped |= XINPUT_B;
    if (b & 0x4000) mapped |= XINPUT_X;
    if (b & 0x8000) mapped |= XINPUT_Y;

    output->Gamepad.wButtons = mapped;
    output->Gamepad.bAnalogButtons[0] = (b & 0x1000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[1] = (b & 0x2000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[2] = (b & 0x4000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[3] = (b & 0x8000) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[4] = (b & 0x0100) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[5] = (b & 0x0200) ? 0xFF : 0;
    output->Gamepad.bAnalogButtons[6] = host.Gamepad.bLeftTrigger;
    output->Gamepad.bAnalogButtons[7] = host.Gamepad.bRightTrigger;
    output->Gamepad.sThumbLX = host.Gamepad.sThumbLX;
    output->Gamepad.sThumbLY = host.Gamepad.sThumbLY;
    output->Gamepad.sThumbRX = host.Gamepad.sThumbRX;
    output->Gamepad.sThumbRY = host.Gamepad.sThumbRY;
    return ERROR_SUCCESS_VALUE;
}

extern "C" uint32_t __cdecl XboxInput_SetState(uint32_t userIndex, const XBOX_INPUT_VIBRATION* vibration) {
    if (!g_started || !vibration || userIndex >= 4 || !g_setState)
        return ERROR_DEVICE_NOT_CONNECTED_VALUE;
    WIN_VIBRATION host{ vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed };
    return g_setState(userIndex, &host);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
