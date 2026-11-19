# Kernel export mapping — Group B

Branch: `work/kernel-export-half-b`
Base: `work/kernel-execution-backend` at `6076f0a144df82340d4d943b62b84e3dba199b71`

## Status

All 57 Group B ordinals have explicit `KernelDispatch` entries. Six exports now have real guest-ABI handlers and x86 dispatch smoke coverage (four object-reference APIs, `NtSetSystemTime`, and `NtOpenDirectoryObject`); the other 51 still route to `K_Unsupported(ordinal)` and remain behaviorally unsupported. A dispatch entry alone is not implementation.

No signatures, structure layouts, host behavior, or calling conventions are asserted by this mapping-only change. Each API must be implemented in a follow-up with pinned-NXDK provenance, x86 guest-call tests, and appropriate host tests before its status can be upgraded.

## Mapped exports

| Ordinal | Export | Status |
|---:|---|---|
| 201 | `NtOpenDirectoryObject` | Implemented for named directories in the shared object namespace; duplicates typed guest handles; x86 ordinal-dispatch tested; access checks/root-relative lookup remain unsupported |
| 203 | `NtOpenSymbolicLinkObject` | Explicit fail-fast route; behavior unsupported |
| 206 | `NtQueueApcThread` | Explicit fail-fast route; behavior unsupported |
| 208 | `NtQueryDirectoryObject` | Explicit fail-fast route; behavior unsupported |
| 215 | `NtQuerySymbolicLinkObject` | Explicit fail-fast route; behavior unsupported |
| 220 | `NtReadFileScatter` | Explicit fail-fast route; behavior unsupported |
| 228 | `NtSetSystemTime` | Implemented through title-local virtual clock offset; x86 ordinal-dispatch tested; host Windows clock remains unchanged |
| 232 | `NtUserIoApcDispatcher` | Explicit fail-fast route; behavior unsupported |
| 237 | `NtWriteFileGather` | Explicit fail-fast route; behavior unsupported |
| 239 | `ObCreateObject` | Explicit fail-fast route; behavior unsupported |
| 241 | `ObInsertObject` | Explicit fail-fast route; behavior unsupported |
| 242 | `ObMakeTemporaryObject` | Explicit fail-fast route; behavior unsupported |
| 243 | `ObOpenObjectByName` | Explicit fail-fast route; behavior unsupported |
| 244 | `ObOpenObjectByPointer` | Explicit fail-fast route; behavior unsupported |
| 246 | `ObReferenceObjectByHandle` | Implemented for shared-manager handles when `ObjectType == NULL`; x86 ordinal-dispatch tested; type descriptors/access checks remain unsupported |
| 247 | `ObReferenceObjectByName` | Explicit fail-fast route; behavior unsupported |
| 248 | `ObReferenceObjectByPointer` | Implemented for shared-manager object bodies when `ObjectType == NULL`; x86 ordinal-dispatch tested; desired-access/access-mode checks remain unsupported |
| 250 | `ObfDereferenceObject` | Implemented for shared-manager object bodies; final reference invokes registered destructor; x86 FASTCALL ordinal-dispatch tested |
| 251 | `ObfReferenceObject` | Implemented for shared-manager object bodies; x86 FASTCALL ordinal-dispatch tested |
| 252 | `PhyGetLinkState` | Explicit fail-fast route; behavior unsupported |
| 253 | `PhyInitialize` | Explicit fail-fast route; behavior unsupported |
| 255 | `PsCreateSystemThreadEx` | Explicit fail-fast route; behavior unsupported |
| 257 | `PsSetCreateThreadNotifyRoutine` | Explicit fail-fast route; behavior unsupported |
| 312 | `RtlUnwind` | Explicit fail-fast route; behavior unsupported |
| 327 | `XeLoadSection` | Explicit fail-fast route; behavior unsupported |
| 328 | `XeUnloadSection` | Explicit fail-fast route; behavior unsupported |
| 329 | `READ_PORT_BUFFER_UCHAR` | Explicit fail-fast route; behavior unsupported |
| 330 | `READ_PORT_BUFFER_USHORT` | Explicit fail-fast route; behavior unsupported |
| 331 | `READ_PORT_BUFFER_ULONG` | Explicit fail-fast route; behavior unsupported |
| 332 | `WRITE_PORT_BUFFER_UCHAR` | Explicit fail-fast route; behavior unsupported |
| 333 | `WRITE_PORT_BUFFER_USHORT` | Explicit fail-fast route; behavior unsupported |
| 334 | `WRITE_PORT_BUFFER_ULONG` | Explicit fail-fast route; behavior unsupported |
| 335 | `XcSHAInit` | Explicit fail-fast route; behavior unsupported |
| 336 | `XcSHAUpdate` | Explicit fail-fast route; behavior unsupported |
| 337 | `XcSHAFinal` | Explicit fail-fast route; behavior unsupported |
| 338 | `XcRC4Key` | Explicit fail-fast route; behavior unsupported |
| 339 | `XcRC4Crypt` | Explicit fail-fast route; behavior unsupported |
| 340 | `XcHMAC` | Explicit fail-fast route; behavior unsupported |
| 341 | `XcPKEncPublic` | Explicit fail-fast route; behavior unsupported |
| 342 | `XcPKDecPrivate` | Explicit fail-fast route; behavior unsupported |
| 343 | `XcPKGetKeyLen` | Explicit fail-fast route; behavior unsupported |
| 344 | `XcVerifyPKCS1Signature` | Explicit fail-fast route; behavior unsupported |
| 345 | `XcModExp` | Explicit fail-fast route; behavior unsupported |
| 346 | `XcDESKeyParity` | Explicit fail-fast route; behavior unsupported |
| 347 | `XcKeyTable` | Explicit fail-fast route; behavior unsupported |
| 348 | `XcBlockCrypt` | Explicit fail-fast route; behavior unsupported |
| 349 | `XcBlockCryptCBC` | Explicit fail-fast route; behavior unsupported |
| 350 | `XcCryptService` | Explicit fail-fast route; behavior unsupported |
| 351 | `XcUpdateCrypto` | Explicit fail-fast route; behavior unsupported |
| 359 | `IoMarkIrpMustComplete` | Explicit fail-fast route; behavior unsupported |
| 365 | `HalEnableSecureTrayEject` | Explicit fail-fast route; behavior unsupported |
| 366 | `HalWriteSMCScratchRegister` | Explicit fail-fast route; behavior unsupported |
| 374 | `MmDbgAllocateMemory` | Explicit fail-fast route; behavior unsupported |
| 375 | `MmDbgFreeMemory` | Explicit fail-fast route; behavior unsupported |
| 376 | `MmDbgQueryAvailablePages` | Explicit fail-fast route; behavior unsupported |
| 377 | `MmDbgReleaseAddress` | Explicit fail-fast route; behavior unsupported |
| 378 | `MmDbgWriteCheck` | Explicit fail-fast route; behavior unsupported |

## Validation required before claiming implementation

- Verify the exact prototype and x86 calling convention against NXDK revision `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`.
- Verify guest structure packing, pointer widths, status values, and ownership rules.
- Implement behavior or retain explicit unsupported status when the host backend cannot safely support it.
- Add guest import-thunk tests that call the ordinal through the actual dispatcher; add host unit tests for host-side behavior.
- Keep unsupported crypto, port-I/O, kernel object, thread, and section APIs from silently forwarding guest pointers into host APIs.

## Scope boundaries

- The I/O completion, file/device I/O, symbolic-link, and synchronization APIs already being worked on by Developers 3 and 4 must be coordinated rather than duplicated.
- Kernel object manager exports must use the shared guest object-handle manager from Developer 2.
- Thread/APC exports require the execution owner's approved thread identity and per-thread TLS lifecycle contract.
- The four object-reference handlers are implemented in `kernel_object_reference.cpp` and routed by `KernelDispatch`; their current limitations are recorded in `docs/KERNEL_OBJECT_REFERENCE.md`. This does not fix the independent real-XBE fault at `0x10018DC0`.
