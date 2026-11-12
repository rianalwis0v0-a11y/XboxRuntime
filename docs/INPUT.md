# Controller input translation

The `input.dll` module adapts Windows XInput controllers to the original Xbox input-state layout.

## Host-side API

Include `modules/input/input.h`.

- `ModuleStart` loads the first available Windows provider: XInput 1.4, 1.3, then 9.1.0.
- `XboxInput_GetConnectedMask` reports connected user slots 0–3.
- `XboxInput_GetState` translates Windows digital buttons, sticks, and triggers into the 18-byte original Xbox gamepad structure and DWORD-aligned 24-byte state structure.
- `XboxInput_SetState` forwards left/right motor speeds to Windows XInput vibration.

The public structs intentionally use the original Xbox layout: 18-byte gamepad, 24-byte DWORD-aligned state, and 4-byte vibration structure.

## Current limitations

- This module currently provides the host-side translation API and is loaded by the runtime.
- The XBE guest import resolver does not yet bind non-kernel XAPI imports such as the game's XInput entry points to this module. Therefore, this commit does not yet establish end-to-end controller input inside retail games.
- Windows XInput is the current host backend. Raw HID, DirectInput-only devices, keyboard mapping, and controller remapping are not implemented.
- Vibration is passed through without per-game calibration.

The `input_smoke` test checks ABI sizes, invalid-user handling, and the connected-slot mask. A physical controller test is still needed.
