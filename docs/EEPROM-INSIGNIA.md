# EEPROM storage and Insignia setup

XboxRuntime stores one persistent EEPROM image per Windows installation at:

`<Windows drive>:\\XboxRuntime\\system\\eeprom.bin`

For example, if Windows is installed on C:, the file is:

`C:\\XboxRuntime\\system\\eeprom.bin`

The native x86 guest exposes the Xbox kernel SMBus exports used to access the EEPROM:

- `HalReadSMBusValue` (kernel ordinal 45)
- `HalWriteSMBusValue` (kernel ordinal 50)
- EEPROM software address: `0xA8`
- Capacity: exactly 256 bytes

Reads and writes are persisted to the file. The runtime creates a 256-byte all-`0xFF` image on first launch only as an **unprovisioned placeholder**. It is not a valid manufactured Xbox identity and is not expected to pass Insignia registration. A file with a size other than 256 bytes is rejected rather than silently replaced.

## Insignia

Do not upload or share the raw EEPROM file or its HDD key. The EEPROM contains device-specific identity and cryptographic material. To connect, use the official Insignia Setup Assistant and follow its supported registration flow: https://insignia.live/guide/connect/

A valid, legally obtained EEPROM backup may be placed at the path above before first launch. Keep a private backup. Do not copy one console's EEPROM to multiple installations, and do not expect the unprovisioned placeholder to register.

This implementation provides persistent SMBus byte/word access for the EEPROM device only. It does not implement the SMC, all SMBus devices, full EEPROM cryptographic validation, or Insignia account/network services.
