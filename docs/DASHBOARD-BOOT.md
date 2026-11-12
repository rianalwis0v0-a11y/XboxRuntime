# Dashboard build and launch

The host startup chain is:

1. `boot.exe` displays `XboxRuntime\\assets\\startup.avi` when that optional file exists.
2. `boot.exe` loads `Windows\\System32\\main.dll` and calls `MainStart`.
3. `main.dll` loads runtime modules from `Windows\\System32`.
4. The final `xbe_loader.dll` module must export `LoadXbe(const wchar_t*)`.
5. `main.dll` asks that loader to launch `XboxRuntime\\dashboard.xbe`.

## Dashboard payload

The dashboard executable is an Xbox XBE, not a Windows executable. The runtime build does not include Microsoft's copyrighted retail dashboard or its assets. Supply a legally obtained dashboard XBE and its required files yourself, placing the launch image at:

```text
X:\XboxRuntime\dashboard.xbe
```

The current launcher expects the dashboard path above. A future dashboard package may provide a homebrew dashboard instead, but it must still be an XBE supported by the runtime loader.

## Build targets

The top-level CMake project now defines:

- `boot` — Windows GUI startup executable (`boot.exe`)
- `main` — runtime orchestrator DLL (`main.dll`)
- `nv2a` — NV2A graphics bridge DLL
- `insignia_dns` — Insignia DNS service DLL

The remaining runtime DLLs listed by `main.dll` must also be implemented and built before the full runtime can start. The dashboard launch path is wired, but a successful full boot is not yet claimed.
