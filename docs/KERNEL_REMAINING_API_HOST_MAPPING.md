# Remaining Xbox kernel API host-mapping plan

Branch: `work/kernel-combined-clean-import`
Catalogue: `tools/kernel_exports.json` (371 exports)
Inputs: Group A status ledger and Group B mapping ledger.
Scope: 84 exports remain partial or explicitly unsupported after six Group B exports were implemented and wired into the guest dispatcher. This is a host-backend design map, **not** a claim that these APIs are implemented.


## Implemented since the initial mapping

- `NtOpenDirectoryObject` (201): named-directory lookup and duplicated typed guest handles.
- `NtSetSystemTime` (228): title-local virtual clock offset; host Windows clock is unchanged.
- `ObReferenceObjectByHandle` (246), `ObReferenceObjectByPointer` (248), `ObfDereferenceObject` (250), and `ObfReferenceObject` (251): shared object-manager references and destruction lifecycle.

These have guest ordinal-dispatch smoke tests; limitations remain documented in the Group B ledger and object-reference implementation notes.

## Mapping rules

1. Keep Xbox x86 ABI, structure layouts, status codes, ownership, and calling convention in the guest compatibility layer.
2. Map guest behavior to host primitives only behind validated adapters. Never pass guest pointers or Xbox handles directly to Win32.
3. Prefer existing XboxRuntime managers (object handles, guest memory, thread/TLS, filesystem bridge, I/O completion) before adding a second subsystem.
4. Where Win32 has no equivalent, implement a virtual Xbox device/service or retain explicit unsupported behavior. Do not fake success.
5. Add per-export x86 thunk tests and host behavior tests before changing the ledger status.

## Group A — 33 partial or unsupported exports

| Ordinal | Xbox export | Current status | Closest host mapping / implementation direction |
|---:|---|---|---|
| 2 | AvSendTVEncoderOption | unsupported | No generic Win32 equivalent. Route to a virtual AV/TV-encoder device owned by the display backend; unsupported until that device models the requested option. |
| 3 | AvSetDisplayMode | unsupported | DXGI output mode enumeration/switching or ChangeDisplaySettingsEx may provide host display operations, but Xbox mode/encoder semantics require an AV adapter. |
| 25 | ExReadWriteRefurbInfo | unsupported | No Windows equivalent. Model the Xbox refurbishment record as validated persistent virtual EEPROM/NVRAM data; preserve documented access restrictions. |
| 38 | HalClearSoftwareInterrupt | partial | Internal virtual IRQL/software-interrupt pending-bit scheduler. No direct Win32 equivalent; do not map to SetThreadPriority. |
| 39 | HalDisableSystemInterrupt | partial | Virtual interrupt-controller mask state. A host event/dispatcher can suppress delivery; no user-mode API provides Xbox bus-IRQ semantics. |
| 43 | HalEnableSystemInterrupt | partial | Virtual interrupt-controller enable/mode state, dispatched through the runtime's device/event loop. |
| 44 | HalGetInterruptVector | unsupported | Runtime-owned virtual bus-to-vector routing table; no direct Windows equivalent. |
| 46 | HalReadWritePCISpace | unsupported | A PCI configuration-space model owned by the virtual chipset/device backend. Win32 has no safe general user-mode PCI config-space equivalent. |
| 48 | HalRequestSoftwareInterrupt | partial | Queue a software interrupt in the virtual dispatcher and deliver at the guest's permitted IRQL; no direct Win32 equivalent. |
| 59 | IoAllocateIrp | unsupported | Implement a guest-layout-verified IRP object allocator in the Xbox I/O manager; host heap allocation is only the backing allocator. |
| 60 | IoBuildAsynchronousFsdRequest | unsupported | Build a verified guest IRP/IO_STACK_LOCATION and submit to the runtime I/O dispatcher; overlapped I/O can back asynchronous host operations. |
| 61 | IoBuildDeviceIoControlRequest | unsupported | Build a verified guest IRP for a device-control request and bind it to the virtual device dispatch table; DeviceIoControl is only a possible host-device adapter. |
| 62 | IoBuildSynchronousFsdRequest | unsupported | Build a verified guest IRP plus guest event/status block; use an internal completion object or host event for waiting. |
| 72 | IoFreeIrp | unsupported | Free only IRPs allocated/initialized by the runtime IRP manager, with completion and ownership checks. |
| 73 | IoInitializeIrp | unsupported | Initialize the verified Xbox guest IRP and stack layout; no direct Win32 equivalent. |
| 77 | IoQueueThreadIrp | unsupported | Link an IRP to the runtime's per-thread pending-I/O list; use runtime thread identity, not a host thread ID as a guest pointer. |
| 84 | IoSynchronousDeviceIoControlRequest | unsupported | Synchronous wrapper over the runtime IRP/device dispatcher; optionally adapt selected virtual/host devices through DeviceIoControl. |
| 85 | IoSynchronousFsdRequest | unsupported | Synchronous wrapper over the runtime file-system/I/O dispatcher and completion event. |
| 86 | IofCallDriver | unsupported | Dispatch through a verified guest DEVICE_OBJECT driver table and IRP stack. No direct Win32 equivalent. |
| 87 | IofCompleteRequest | unsupported | Runtime IRP completion engine: unwind stack locations, invoke validated completion routines, signal events/completion ports, and release ownership. |
| 98 | KeConnectInterrupt | partial | Register a guest ISR with the virtual interrupt controller; host device events may trigger it, but host callbacks must not run guest code directly. |
| 100 | KeDisconnectInterrupt | partial | Remove the virtual ISR registration and synchronize against in-flight delivery. |
| 139 | KeRestoreFloatingPointState | unsupported | Save/restore the guest x86 FPU/SSE context through the execution backend's per-thread CPU-context layer; Windows thread-context APIs are not a drop-in replacement. |
| 141 | KeRundownQueue | partial | Keep the existing dispatcher queue implementation; verify list layout, ordering, and rundown semantics against pinned NXDK. |
| 142 | KeSaveFloatingPointState | unsupported | Capture guest x87/SSE state in the execution backend's per-thread context; validate exact Xbox KFLOATING_SAVE layout first. |
| 163 | KiUnlockDispatcherDatabase | partial | Keep the existing guest dispatcher/IRQL primitive; host mutex release alone is not equivalent to lowering Xbox IRQL. |
| 168 | MmClaimGpuInstanceMemory | unsupported | Connect to the NV2A GPU instance-memory allocator and expose a guest-visible allocation with correct physical/offset semantics. |
| 175 | MmLockUnlockBufferPages | partial | Existing VirtualLock/VirtualUnlock approximation; retain explicit limitation that host residency is not Xbox physical pinning. |
| 176 | MmLockUnlockPhysicalPage | unsupported | Requires guest physical-page database and pin/reference accounting; no direct Win32 equivalent. |
| 177 | MmMapIoSpace | unsupported | Map only approved virtual device/MMIO regions through the chipset/GPU device bus; never map arbitrary host physical addresses. |
| 178 | MmPersistContiguousMemory | partial | Existing VirtualLock/VirtualUnlock approximation; real support requires contiguous guest physical-memory allocation and residency tracking. |
| 181 | MmQueryStatistics | unsupported | Report runtime-owned Xbox pool/page/object counters using a verified guest MM_STATISTICS layout; do not return host process statistics as if they were Xbox values. |
| 183 | MmUnmapIoSpace | unsupported | Release a mapping created by the runtime MMIO registry, validating base/length and mapping ownership. |

## Group B — 51 explicitly unsupported exports (six implemented exports excluded)

| Ordinal | Xbox export | Closest host mapping / implementation direction |
|---:|---|---|
| 201 | NtOpenDirectoryObject | Open a typed entry in the runtime object namespace; do not use a raw Win32 HANDLE as the guest object. |
| 203 | NtOpenSymbolicLinkObject | Open a runtime symbolic-link object and reference it through the shared object manager. |
| 206 | NtQueueApcThread | Queue an APC record to the runtime guest-thread scheduler; deliver only at a safe guest alertable point. |
| 208 | NtQueryDirectoryObject | Enumerate the runtime object namespace with Xbox directory-information layout and restart/context semantics. |
| 215 | NtQuerySymbolicLinkObject | Query the target stored in the runtime symbolic-link object. |
| 220 | NtReadFileScatter | Implement scatter reads through the runtime file provider and validated guest buffer array; ReadFileScatter is a possible host adapter only for compatible aligned files/buffers. |
| 228 | NtSetSystemTime | Virtualize guest system time using a runtime clock offset; SetSystemTime is not appropriate because it changes the user's host clock. |
| 232 | NtUserIoApcDispatcher | Implement the Xbox user-I/O APC dispatch ABI in the guest execution/APC layer; no direct Win32 equivalent. |
| 237 | NtWriteFileGather | Implement gather writes through the runtime file provider and validated guest buffer array; WriteFileGather is only a constrained host adapter. |
| 239 | ObCreateObject | Allocate a typed object with Xbox header/type/name/lifetime metadata in the shared object manager. |
| 241 | ObInsertObject | Insert a typed object into the runtime namespace and create a guest handle using Xbox access/attribute rules. |
| 242 | ObMakeTemporaryObject | Clear the permanent flag in the runtime object header and allow deletion after references reach zero. |
| 243 | ObOpenObjectByName | Resolve a name in the runtime object namespace, check object type/access, then publish a guest handle. |
| 244 | ObOpenObjectByPointer | Validate a runtime-owned guest object pointer and create a handle; never accept arbitrary host pointers. |
| 246 | ObReferenceObjectByHandle | Resolve a guest handle through the shared handle manager and increment the typed object's reference count. |
| 247 | ObReferenceObjectByName | Resolve a named object and increment its reference count with optional type/access checks. |
| 248 | ObReferenceObjectByPointer | Validate and reference a runtime-managed object pointer. |
| 250 | ObfDereferenceObject | Decrement the runtime object's reference count and run its destructor at the correct lifetime boundary. |
| 251 | ObfReferenceObject | Increment the runtime object's reference count with overflow/lifetime validation. |
| 252 | PhyGetLinkState | Query a virtual Ethernet PHY owned by the network backend; map to host adapter link state only as a documented approximation. |
| 253 | PhyInitialize | Initialize the virtual Ethernet PHY/network adapter state; no direct Win32 equivalent. |
| 255 | PsCreateSystemThreadEx | Create a runtime guest system thread with Xbox start/context/stack/TLS semantics; CreateThread may back execution but is not the guest API. |
| 257 | PsSetCreateThreadNotifyRoutine | Register callbacks in the runtime thread lifecycle and invoke them on guest-thread create/exit transitions. |
| 312 | RtlUnwind | Implement x86 guest exception-frame unwinding in the guest execution backend; RtlUnwindEx is not safe to call on guest frames in the host process. |
| 327 | XeLoadSection | Load/relocate/protect the requested XBE section through the XBE loader and guest memory manager. |
| 328 | XeUnloadSection | Unmap/release a section through the XBE loader with execution/reference safety checks. |
| 329 | READ_PORT_BUFFER_UCHAR | Read a byte sequence from a registered virtual I/O port device; no general user-mode Win32 equivalent. |
| 330 | READ_PORT_BUFFER_USHORT | Read 16-bit values from a registered virtual I/O port device with width/alignment validation. |
| 331 | READ_PORT_BUFFER_ULONG | Read 32-bit values from a registered virtual I/O port device with width/alignment validation. |
| 332 | WRITE_PORT_BUFFER_UCHAR | Write byte values to a registered virtual I/O port device. |
| 333 | WRITE_PORT_BUFFER_USHORT | Write 16-bit values to a registered virtual I/O port device with width/alignment validation. |
| 334 | WRITE_PORT_BUFFER_ULONG | Write 32-bit values to a registered virtual I/O port device with width/alignment validation. |
| 335 | XcSHAInit | Back with BCrypt SHA-1 hash-provider state, after confirming Xbox state-buffer ABI and lifecycle. |
| 336 | XcSHAUpdate | Feed validated input to the runtime crypto hash context; BCryptHashData is a candidate backend call. |
| 337 | XcSHAFinal | Finalize the Xbox SHA context using BCryptFinishHash and copy to a validated guest output buffer. |
| 338 | XcRC4Key | Implement the Xbox RC4 key-scheduling state with a verified guest context layout; do not assume BCrypt exposes compatible RC4 support on every Windows version. |
| 339 | XcRC4Crypt | Implement the Xbox RC4 stream state and byte-for-byte behavior; use a vetted internal implementation if the host provider does not support RC4. |
| 340 | XcHMAC | Back HMAC with BCrypt HMAC/hash APIs after matching Xbox algorithm identifiers, key handling, and output lengths. |
| 341 | XcPKEncPublic | Use BCrypt asymmetric public-key import/encrypt operations only after matching Xbox key-blob and padding formats. |
| 342 | XcPKDecPrivate | Use BCrypt private-key operations only with verified Xbox key representation and padding; private-key availability/ownership must be explicit. |
| 343 | XcPKGetKeyLen | Parse validated Xbox key structures and return the documented bit/byte length; no host crypto call is required. |
| 344 | XcVerifyPKCS1Signature | Verify with BCrypt RSA PKCS#1 operations after matching digest identifiers, key encoding, and signature semantics. |
| 345 | XcModExp | Implement modular exponentiation with checked big-integer arithmetic or a vetted crypto provider; preserve Xbox byte order and buffer sizes. |
| 346 | XcDESKeyParity | Implement DES odd-parity adjustment on the guest key bytes; simple deterministic transform, not a host OS service. |
| 347 | XcKeyTable | Recreate the documented Xbox crypto key table as runtime-owned immutable data; do not substitute unrelated Windows key stores. |
| 348 | XcBlockCrypt | Implement the Xbox block-cipher mode/key schedule using verified algorithm and buffer ABI; BCrypt is a candidate only if semantics match. |
| 349 | XcBlockCryptCBC | Implement CBC chaining and padding exactly as Xbox expects, with validated IV/key/buffer sizes. |
| 350 | XcCryptService | Model the Xbox cryptographic service state machine and route supported operations to the runtime crypto backend. |
| 351 | XcUpdateCrypto | Update runtime crypto service/key state according to verified Xbox semantics; no direct Windows equivalent. |
| 359 | IoMarkIrpMustComplete | Set the verified must-complete state bit in the runtime-owned IRP representation. |
| 365 | HalEnableSecureTrayEject | Send the operation to a virtual SMC/tray device; no generic Win32 equivalent. |
| 366 | HalWriteSMCScratchRegister | Store/write the virtual SMC scratch register through the SMC device model. |
| 374 | MmDbgAllocateMemory | Allocate guest debug memory through the guest memory manager (VirtualAlloc-backed storage may be used internally). |
| 375 | MmDbgFreeMemory | Release only allocations tracked by the guest debug-memory allocator. |
| 376 | MmDbgQueryAvailablePages | Query runtime guest physical-page accounting, not host free RAM. |
| 377 | MmDbgReleaseAddress | Release the matching guest debug address reservation through the guest memory manager. |
| 378 | MmDbgWriteCheck | Validate guest writable memory ranges using the guest memory map/protection metadata. |

## Recommended implementation order

1. **Object manager:** ordinals 201, 203, 208, 215, 239, 241–244, 246–248, 250–251. Reuse the shared typed object/handle manager.
2. **IRP/I/O manager:** ordinals 59–62, 72–73, 77, 84–87, 220, 237, 359. First pin exact x86 IRP/IO_STACK_LOCATION layouts and ownership rules; then build one dispatch/completion pipeline.
3. **Thread/APC/time:** ordinals 206, 228, 232, 255, 257, 312, 139, 142. Requires execution-backend thread identity, TLS, exception and APC contracts.
4. **Crypto:** ordinals 335–351. Verify every prototype/state buffer against the pinned NXDK revision before selecting BCrypt or internal implementations.
5. **Memory/GPU/MMIO:** ordinals 168, 176–178, 181, 183, 374–378. Coordinate with guest memory manager and NV2A/device bus; do not expose arbitrary host physical mappings.
6. **Hardware/AV/network:** ordinals 2, 3, 44, 46, 252, 253, 365, 366 and port-buffer I/O 329–334. Implement virtual devices and connect them to NV2A, network, chipset and SMC owners.
7. **Finish partial primitives:** ordinals 38, 39, 43, 48, 98, 100, 141, 163, 175, 178 with stronger integration tests and explicit residual limitations.

## Exit criteria

An export moves from unsupported/partial to implemented only after:
- Prototype, ordinal, calling convention, structure layout and ownership are verified against pinned NXDK.
- Guest pointers/ranges and all lengths are validated.
- The x86 import thunk calls the export and checks observable behavior.
- Host tests cover success, invalid input, boundary conditions and lifetime/concurrency.
- Unsupported hardware behavior remains explicit rather than returning fabricated success.
- The machine-readable ledger and this plan are updated together.
