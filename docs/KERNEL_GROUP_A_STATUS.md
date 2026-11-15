# Group A kernel export implementation status

Pinned ABI reference: XboxDev/nxdk `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`

Branch: `work/kernel-export-half-a`  
PR: https://github.com/rianalwis0v0-a11y/XboxRuntime/pull/22  
Target: `work/kernel-execution-backend`

## Coverage

All 57 assigned Group A ordinals have exactly one `KernelDispatch` comparison and a one-line handler jump. This is dispatch coverage, not a claim that every API is implemented.

| Status | Count |
|---|---:|
| Implemented and tested | 23 |
| Partial implementation, tested | 6 |
| Explicitly unsupported | 28 |
| **Total** | **57** |

The machine-readable per-export status, handler, evidence and limitations are in `tools/kernel_export_group_a.json`.

## Implemented and tested

- EEPROM settings: `ExQueryNonVolatileSetting`, `ExSaveNonVolatileSetting` (24, 29).
- File operations: `IoCreateFile`, `IoQueryFileInformation`, `IoQueryVolumeInformation` (66, 75, 76). The query helpers currently accept the adapter's handle-backed file-object representation.
- Share access: `IoCheckShareAccess`, `IoRemoveShareAccess`, `IoSetShareAccess` (63, 78, 80), using the pinned 72-byte x86 FILE_OBJECT layout and 7-byte SHARE_ACCESS layout.
- Device/packet handling: `IoCreateDevice`, `IoDeleteDevice`, `IoStartPacket`, `IoStartNextPacket`, `IoStartNextPacketByKey`, `IoDismountVolume` (65, 68, 83, 81, 82, 90). Device objects use the pinned 72-byte x86 layout, with a 68-byte DRIVER_OBJECT and 12-byte KDEVICE_QUEUE. The packet queue is adapter-side metadata; named devices and full IRP stack integration are not implemented.
- I/O completion: `IoSetIoCompletion` (79), backed by typed completion objects and a host I/O completion port. Related NtCreate/Set/Remove/QueryIoCompletion helpers are also routed for testing.
- Thread/APC: `KeAlertThread`, `KeInitializeApc`, `KeInsertQueueApc`, `KeInitializeInterrupt`, `KeSynchronizeExecution` (93, 105, 118, 109, 153).
- Memory: `MmCreateKernelStack`, `MmDeleteKernelStack` (169, 170).
- Object namespace: `NtCreateDirectoryObject` (188), using typed object handles and duplicate-name checks.

## Partial implementations

- `HalClearSoftwareInterrupt` and `HalRequestSoftwareInterrupt` (38, 48): pending software-IRQL bookkeeping is implemented and tested; there is not yet a complete software interrupt dispatcher.
- `KeRundownQueue` and `KiUnlockDispatcherDatabase` (141, 163): queue rundown and the lower-IRQL primitive are tested, but this is not a complete dispatcher/scheduler.
- `MmLockUnlockBufferPages` and `MmPersistContiguousMemory` (175, 178): use VirtualLock/VirtualUnlock as a host-backed residency approximation, not Xbox physical-page pinning or physical-contiguity guarantees.

## Explicitly unsupported

The remaining 28 exports fail explicitly rather than pretending success. The largest gap is the IRP/device-driver stack: IRP and IO_STACK_LOCATION layouts are not fully defined by the pinned public NXDK header, so the IRP allocation/build/free, IofCallDriver/completion, thread-IRP and synchronous dispatch exports remain unsupported. Hardware-dependent display, TV encoder, PCI, physical interrupt, GPU/MMIO and physical-page APIs also remain unsupported. Exact reasons are recorded per ordinal in the ledger.

## Verification

- Group A EEPROM CI: https://github.com/rianalwis0v0-a11y/XboxRuntime/actions/runs/43697175322 — x64 host and x86 guest tests passed.
- Expanded x86/host CI: https://github.com/rianalwis0v0-a11y/XboxRuntime/actions/runs/43708119859 — host tests passed; all six x86 guest smoke tests passed, including memory, APC/thread, share access, device packet scheduling, I/O completion and directory object tests.
- The full kernel workflow also runs the export catalogue audit; the latest run has passed that audit and is continuing through backend builds/integration.

No merge to the integration branch has been performed. PR #22 remains draft pending review and the remaining 28 exports.
