# XBE loader and native x86 guest process

## Architecture

The runtime host and hardware modules are x64. XBE code is 32-bit x86, so the
host does not try to call a 32-bit entry point from its 64-bit address space.
Instead, `xbe_loader.dll` starts the adjacent `xbe_guest32.exe` helper.

The helper is compiled as Win32 and runs x86 instructions directly on the
machine's x86-64 CPU through Windows' WoW64 compatibility layer. This is not
a CPU emulator and does not translate every guest instruction. A 64-bit
Windows PE image must include the WoW64 optional component to launch this
32-bit helper.

The two processes communicate over a private, per-process named pipe. Only a
small fixed-size status record crosses the pipe; the XBE file is read and
mapped inside the x86 process, so the image is not copied through IPC. The
guest process reserves the XBE's declared 32-bit image base, copies validated
headers/sections, zero-fills virtual tails, and applies section page
protections. The helper remains alive with that address space reserved until
the host unloads the image or shuts down.

## Current state

- The x64 loader starts the adjacent x86 helper and performs a bounded handshake.
- The x86 helper validates the XBEH header, section table and file ranges.
- The helper maps the image at its declared guest virtual base, copies headers
  and sections, zero-fills virtual tails, and applies section page protections.
- The guest resolves the currently implemented kernel import thunks, prepares
  the supported TLS state, creates a native x86 thread, and calls the XBE entry.
- The guest remains alive until the host unloads the image or shuts down.
- CI builds both architectures and runs a minimal synthetic XBE whose entry
  point is a single `RET`. The test verifies that the guest accepts the image,
  starts the entry thread, and shuts down cleanly.

The smoke test proves only basic native x86 entry dispatch. It does **not**
prove that a retail dashboard or commercial game can boot. The kernel API
surface is partial, and hardware, graphics, audio, input, storage integration,
and networking still need substantial implementation and testing.

## Exports

- `ModuleStart()` / `ModuleStop()`: loader lifecycle.
- `XbeLoadImage(path)`: start the x86 guest and map one image. Returns
  `XBE_E_NO_KERNEL` (10) when mapping succeeded but kernel startup is pending.
- `XbeUnloadImage()`: stop the guest process and release its address space.
- `XbeGetImageInfo(...)`: retrieve mapped image metadata.
- `LoadXbe(path)`: host startup ABI; returns zero when the guest image is
  staged, not when game execution has begun.

## Build and deployment

The main workflow builds:
- x64: `boot.exe`, `main.dll`, `nv2a.dll`, `insignia_dns.dll`,
  `fatx.dll`, `xbe_loader.dll`
- x86: `xbe_guest32.exe`

Keep `xbe_guest32.exe` in the same directory as `xbe_loader.dll`. The CI
workflow stages both under `build/out` and verifies both files.

## Remaining work

Retail executable decryption, complete kernel import coverage, accurate Xbox
process/thread semantics, hardware aliases, guest exception handling, and the
full Xbox kernel ABI are not implemented. The current import stubs cover only
a limited set of APIs and deliberately stop on unsupported imports. The guest process currently maps
the XBE and waits; it does not jump to the entry point. The NV2A and other
hardware services still need guest-facing interfaces.
