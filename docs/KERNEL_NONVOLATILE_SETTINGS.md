# Group A: EEPROM-backed nonvolatile settings

## Implemented exports

- Ordinal 24, `ExQueryNonVolatileSetting`
- Ordinal 29, `ExSaveNonVolatileSetting`

Both use the runtime's existing per-title 256-byte EEPROM image, the same backing bytes exposed by the SMBus EEPROM address (0xA8). They do not create a second, disconnected settings store.

The export signatures and calling convention are taken from `XboxDev/nxdk@fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`, `lib/xboxkrnl/xboxkrnl.h`; ordinal and decorated x86 export names are verified against the same revision's `lib/xboxkrnl/xboxkrnl.exe.def`.

## Query behavior

- Supports the documented OS setting indices 0x0000–0x0012 and XC_MAX_OS (0x00FF), the complete 0x60-byte user-settings section.
- Index 0xFFFF returns the complete raw 256-byte EEPROM image as REG_BINARY.
- Returns the exact value type and required byte count; a short caller buffer returns `STATUS_BUFFER_TOO_SMALL` without modifying the caller's buffer.
- Unknown and not-yet-verified factory indices return `STATUS_INVALID_PARAMETER`.
- EEPROM access before successful initialization returns `STATUS_DEVICE_NOT_CONNECTED`.

## Save behavior

- Supports only the documented user-configurable indices: standard timezone name/date, language, video, audio, game/movie parental controls, parental passcode, and misc flags.
- Requires the exact expected registry type and byte length.
- Read-only, factory, encrypted, and unknown values are not modified; read-only known values return `STATUS_NOT_SUPPORTED`.
- Updates the user-section XConfig checksum at offset 0x60 and flushes the complete 256-byte image to the configured EEPROM file.
- Raw SMBus writes remain raw hardware writes and do not silently rewrite checksums.

The setting-index mapping comes from the pinned NXDK `XC_*` definitions. EEPROM byte offsets, writable-field classifications, and the user-section checksum range are cross-checked against the Original Xbox EEPROM layout reference: https://github.com/feudalnate/Original-Xbox-Data-Structures/blob/master/EEPROM/README.md (sections at 0x60–0xBF; user data 0x64–0xBF).

## Explicitly unsupported Group A exports

- Ordinal 25, `ExReadWriteRefurbInfo`: returns `STATUS_NOT_SUPPORTED` after validating the 16-byte x86 `XBOX_REFURB_INFO` layout. The runtime has no verified refurbishment metadata source.
- Ordinal 3, `AvSetDisplayMode`: returns `STATUS_NOT_SUPPORTED`; Xbox register-base and display-mode programming is not mapped to the host display backend.
- Ordinal 2, `AvSendTVEncoderOption`: routed to the existing noncontinuable unsupported-import exception because this void API has no status return and no verified TV-encoder implementation.

## Tests

- `kernel_nvsettings_host_tests`: descriptor and checksum vectors on the x64 host.
- `kernel_nvsettings_guest_smoke`: x86-generated push-ordinal/relative-jump import thunks for ordinals 24 and 29; query/save ABI, type/length validation, persistence/reload, read-only rejection, unsupported refurb/AV return behavior.
- The x86 smoke thunk uses the same 10-byte `push imm32; jmp rel32` encoding as the production import thunk. The test dispatch shim is deliberately isolated from the full production dispatcher; the production `KernelDispatch` routes are also updated for these ordinals.

## Limitations

This does not implement EEPROM cryptographic verification/decryption, factory-index queries/provisioning, encrypted-section writes, full UEM/refurb handling, or actual Xbox AV hardware. A newly created EEPROM remains an all-0xFF unprovisioned image; this implementation does not fabricate console identity keys or a MAC address.
