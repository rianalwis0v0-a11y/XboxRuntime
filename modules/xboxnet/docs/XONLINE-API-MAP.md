# Original Xbox XOnline API map

This document maps the first server-facing compatibility tranche from the
public RXDK-Libs original-Xbox reference. It deliberately does not borrow
Xbox 360 / Games for Windows Live XUser profile-setting structures.

## Primary reference

- [RXDK-Libs public XOnline header](https://github.com/Team-Resurgent/RXDK-Libs/blob/b7c9f2b3a24ece1aec850dca52155a4f8633686c/shared/include/xonline.h)
- [RXDK-Libs XOnline user implementation](https://github.com/Team-Resurgent/RXDK-Libs/blob/b7c9f2b3a24ece1aec850dca52155a4f8633686c/libs/libxonline/src/users.cpp)
- [RXDK-Libs logon implementation](https://github.com/Team-Resurgent/RXDK-Libs/blob/b7c9f2b3a24ece1aec850dca52155a4f8633686c/libs/libxonline/src/logon.cpp)
- [RXDK-Libs task implementation](https://github.com/Team-Resurgent/RXDK-Libs/blob/b7c9f2b3a24ece1aec850dca52155a4f8633686c/libs/libxonline/src/xontask.cpp)

RXDK-Libs is a community-maintained reworked implementation. Its files
identify Microsoft-authored portions and separately licensed Team Resurgent
changes. Use as a technical reference; preserve provenance and applicable
license obligations before copying code.

## Confirmed public API boundaries

The public header declares:

- `XOnlineStartup(const XONLINE_STARTUP_PARAMS*)` and `XOnlineCleanup()`
  for runtime lifetime. The header says XNet must already be initialized
  before XOnline startup.
- `XOnlineGetUsers(XONLINE_USER*, DWORD*)` for enumerating locally stored
  online accounts.
- `XOnlineLogon(const XONLINE_USER*, const DWORD*, DWORD, HANDLE,
  XONLINETASK_HANDLE*)` to begin online logon for selected users/services.
- `XOnlineTaskContinue(XONLINETASK_HANDLE)` to pump asynchronous work.
- Task-specific result retrieval and task-close operations to finish and
  release task resources.

The reference implementation of `XOnlineGetUsers` reads locally stored
account records from the console hard-drive configuration sectors and, on
Xbox builds, memory-unit metadata. This is not the same thing as a remote
server-side "get profile" request.

## Correct implementation split

1. **Guest ABI compatibility:** expose original-Xbox XOnline exports with
   correct Xbox calling convention, structure packing, HRESULT values, and
   task-handle semantics. These belong in the guest compatibility layer.
2. **Local account storage:** model locally stored console accounts and
   account selection separately from remote XboxNet identities. The current
   SQLite `AccountProfileService` is not a replacement for the console's
   HDD/MU account format.
3. **Remote logon backend:** implement the server-side authentication and
   service authorization reached by `XOnlineLogon`. This requires verified
   protocol details and captured traffic; do not invent Kerberos ticket
   formats, cryptographic derivation, or request framing.
4. **Async task model:** preserve the XOnline task lifecycle. A synchronous
   HTTP handler is not a drop-in substitute for `XOnlineTaskContinue` and
   task-specific result functions.

## Platform trap: XUser profile-setting APIs

Search results for `XUserReadProfileSettings` and
`XUserWriteProfileSettings` commonly lead to Xbox 360 / GFWL XLive
implementations. Their structures and signatures must not be assumed to
match original Xbox XOnline. Do not implement them in the OG Xbox path unless
a target-XDK header, original-Xbox binary, or verified XBE import establishes
that exact export.

## Next engineering tasks

- Extract and verify the exact `XONLINE_USER`, `XONLINE_STARTUP_PARAMS`,
  task handle, service ID, and logon result layouts from the target header.
- Add ABI layout tests (size, alignment, offsets) to the guest compatibility
  build.
- Inventory the target XBE's actual XOnline imports before implementing
  additional exports.
- Keep all network-facing logon behavior fail-closed until a real verifier
  and protocol implementation are available.

## Guest32 compatibility implementation

The repository now has an initial implementation in
`guest32/xonline_compat.h` and `guest32/xonline_compat.cpp`:

- `XUID`: 64-bit user/team identifier plus 32-bit user flags.
- `XONLINE_USER`: 112-byte packed structure in this reference layout:
  XUID at offset 0, gamertag at 12, options at 28, passcode at 32,
  72 reserved bytes at 36, and per-user HRESULT at 108.
- `XOnlineStartup` / `XOnlineCleanup`: runtime lifecycle; startup requires
  XNet to be initialized first.
- `XOnlineGetUsers`: currently returns an empty list because console HDD and
  memory-unit account storage are not implemented.
- `XOnlineLogon`: creates a tracked task handle.
- `XOnlineTaskContinue`: completes the task with
  `XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE` until a real backend is configured.
- `XOnlineLogonTaskGetResults` and `XOnlineTaskClose`: expose the task result
  and release its handle.

The guest smoke test checks structure sizes/offsets and the task lifecycle.
This deliberately does **not** report a successful sign-in. It is an ABI and
lifecycle scaffold; real local account persistence, the XboxNet logon protocol,
ticket verification, service authorization, and console testing remain open.
