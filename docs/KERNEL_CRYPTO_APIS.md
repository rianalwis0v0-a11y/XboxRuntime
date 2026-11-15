# Xbox crypto export implementation status

Branch: `work/kernel-crypto-apis`
PR: https://github.com/rianalwis0v0-a11y/XboxRuntime/pull/24
NXDK ABI reference: `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`

## Implemented and tested

| Ordinal | Export | Implementation | Test evidence |
|---:|---|---|---|
| 335 | `XcSHAInit` | Initializes the SHA-1 state at byte offset 24 of the Xbox 116-byte SHA context; preserves the 24-byte prefix | x86 known-answer test |
| 336 | `XcSHAUpdate` | Streaming SHA-1 update over guest input | split-update, multi-block FIPS vector |
| 337 | `XcSHAFinal` | Writes the 20-byte SHA-1 digest and clears the 92-byte inner SHA state | digest and zeroization checks |
| 338 | `XcRC4Key` | Initializes the 264-byte RC4 state (256-byte permutation plus 32-bit i/j state) | RC4 "Key"/"Plaintext" vector |
| 339 | `XcRC4Crypt` | In-place RC4 stream XOR and state advancement | RC4 known-answer test |
| 340 | `XcHMAC` | HMAC-SHA1 over the two input spans; matches the Xbox/Cxbx compatibility behavior of truncating key material to 64 bytes | RFC 2202 known-answer test |
| 346 | `XcDESKeyParity` | Sets the low bit of each key byte to make odd parity | per-byte odd parity assertions |

All seven handlers use the NXDK-declared `NTAPI` signatures (32-bit x86 stdcall). Guest spans are checked with `VirtualQuery` before access. The handlers are wired into `KernelDispatch` and built into `xbe_guest32`.

## ABI layout/provenance notes

The pinned NXDK header declares the prototypes. The SHA context's 24-byte prefix and the RC4 context layout are corroborated by the open Cxbx-Reloaded compatibility implementation:

- https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/kernel/exports/EmuKrnlXc.cpp
- https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/common/crypto/EmuSha.h
- https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/common/crypto/LibRc4.h

The implementation in this branch is written independently; these references are used to verify ABI layout and compatibility behavior, not copied as source.

## Still unsupported

| Ordinal | Export |
|---:|---|
| 341 | `XcPKEncPublic` |
| 342 | `XcPKDecPrivate` |
| 343 | `XcPKGetKeyLen` |
| 344 | `XcVerifyPKCS1Signature` |
| 345 | `XcModExp` |
| 347 | `XcKeyTable` |
| 348 | `XcBlockCrypt` |
| 349 | `XcBlockCryptCBC` |
| 350 | `XcCryptService` |
| 351 | `XcUpdateCrypto` |

These remain explicitly unsupported until their key/structure layouts, semantics, and tests are established. No fake-success stubs are added.
