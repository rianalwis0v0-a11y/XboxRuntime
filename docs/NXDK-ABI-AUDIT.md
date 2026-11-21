# NXDK Xbox ABI reference audit

## Purpose

Use NXDK's maintained original-Xbox headers as the source of truth for the
Xbox kernel scalar types and kernel-facing ABI. Do not treat Windows SDK types
or host pointer widths as proof of an Xbox guest ABI.

## Audited NXDK sources

The following files were inspected from the XboxDev/nxdk `master` tree:

- [`lib/xboxkrnl/xboxdef.h`](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxdef.h)
  - SPDX: CC0-1.0.
  - Defines the Xbox primitive types including BYTE/UCHAR, WORD/USHORT,
    UINT, DWORD, ULONG, BOOL, and ULONG_PTR.
  - The Xbox target is 32-bit; pointer-sized values and ULONG_PTR are
    therefore 32-bit in the guest ABI.
- [`lib/xboxkrnl/xboxkrnl.h`](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h)
  - SPDX: CC0-1.0.
  - Includes xboxdef.h and declares the kernel interface, NTAPI, CDECL,
    FASTCALL, and related kernel calling-convention definitions.
- [`lib/winapi/windef.h`](https://github.com/XboxDev/nxdk/blob/master/lib/winapi/windef.h)
  - SPDX: MIT.
  - Defines WINAPI as __stdcall for the NXDK toolchain and imports
    xboxkrnl/xboxdef.h.

The guest compatibility header now has compile-time width assertions for
BYTE, WORD, UINT, DWORD, and ULONG. These assertions catch accidental host
or toolchain type drift before an ABI test can silently pass.

## Important scope finding

NXDK's current repository tree does **not** contain an XNet/XSocket SDK
declaration header. Its `lib/net` directory contains the lwIP-based network
stack and the original-Xbox network-interface drivers; it is not a drop-in
XNet API implementation.

Therefore:

- Use NXDK xboxdef.h and xboxkrnl.h for Xbox primitive/kernel ABI reference.
- Do not claim that NXDK validates XNetStartupParams, XNADDR, XNDNS,
  XNKID, SOCKADDR, or SOCKAPILIST declarations; those require a separate
  XDK-compatible XNet header/reference and binary-level verification.
- Do not include NXDK's whole kernel headers directly in the Windows host
  build: they are written for NXDK's Clang/GNU-style toolchain and define
  many names that collide with Windows SDK headers.
- Keep the guest process explicitly x86. A 64-bit host SOCKET, HANDLE, or
  pointer must never be exposed as an Xbox guest value.

## Next ABI verification steps

1. Add an authoritative XDK-compatible XNet header as a reference-only
   source and record its provenance/license before importing any declarations.
2. Compare every XNet/XSocket/SOCKAPILIST function name, parameter type,
   return type, WINAPI/stdcall convention, and structure packing.
3. Add compile-time size/offset assertions for XNetStartupParams, XNADDR,
   XNKID, XNKEY, XNDNS, XNQOS, and XNQOSINFO when authoritative definitions
   are available.
4. Verify the actual retail XBE import-library identities and calling
   conventions; never invent kernel ordinals for statically linked XNet code.
5. Add x86 guest-call tests that pass real guest pointers through the loader
   dispatch path, not only host-side wrapper tests.

## Current implementation changes

- `tests/network_smoke.cpp` now waits for the asynchronous DNS completion
  event before reading XNDNS.iStatus and XNDNS.cina. This corrects the stale
  synchronous assumption that caused the latest observed CTest failure.
- `guest32/xnet_compat.h` asserts the NXDK Xbox scalar widths listed above.

These are ABI-hardening and test-correction changes, not proof of complete
retail XNet compatibility or Insignia connectivity.
