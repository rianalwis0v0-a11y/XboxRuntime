# Xbox kernel I/O and system services

Pinned ABI source: XboxDev/nxdk revision `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`, especially `lib/xboxkrnl/xboxkrnl.h`, `xboxdef.h`, and `xboxkrnl.exe.def`.

## Initial audit

The export catalogue is an ordinal inventory, not an implementation ledger. Existing guest dispatch already routes a limited file-I/O subset: `NtCreateFile` (190), `NtOpenFile` (202), `NtReadFile` (219), `NtWriteFile` (236), `NtFlushBuffersFile` (198), `NtDeleteFile` (195), `NtQueryDirectoryFile` (207), `NtQueryInformationFile` (211), `NtSetInformationFile` (226), `NtQueryVolumeInformationFile` (218), and `NtQueryFullAttributesFile` (210). The directory query supports `FileDirectoryInformation` (class 1) and `FileNamesInformation` (class 12); file information queries support only a small subset of classes. `OBJECT_ATTRIBUTES` is the pinned 12-byte x86 layout (`HANDLE RootDirectory`, `PANSI_STRING ObjectName`, `ULONG Attributes`), and root-directory-relative paths are explicitly unsupported. The current read/write handlers complete synchronously and do not implement APC/overlapped completion. `IoCreateSymbolicLink`/`IoDeleteSymbolicLink` now have a bounded guest namespace map used by the existing file-path translator; this is not a complete NT object manager or device-object implementation.

The following catalogue groups remain undispatched in this branch: most IRP/device-object APIs (ordinals 59-87), `NtReadFileScatter` (220), and `NtWriteFileGather` (237). I/O completion exports `NtCreateIoCompletion` (191), `NtQueryIoCompletion` (212), `NtRemoveIoCompletion` (223), and `NtSetIoCompletion` (227) are assigned to Developer 3; the `work/kernel-thread-apis` branch contains initial handlers and dispatch, so this branch deliberately does not duplicate them. Integration and async file-read/write completion remain subject to the threading-team contract.

System-information candidates include `PsQueryStatistics` (256) and `MmQueryStatistics` (181). HAL shutdown notifications are registered through `HalRegisterShutdownNotification` (47). The latter overlaps the memory developer's ownership and is intentionally excluded here. `PsQueryStatistics` is the first isolated implementation in this branch.

## Implemented in this branch

| API | Ordinal | Status | Evidence |
|---|---:|---|---|
| `PsQueryStatistics` | 256 | PARTIAL | x86 guest stdcall smoke plus synthetic XBE import-thunk call; returns process-backing thread/handle counts, not a console-wide Xbox object-manager snapshot |
| `NtCreateFile` | 190 | PARTIAL | synthetic XBE calls through ANSI OBJECT_ATTRIBUTES and an IoCreateSymbolicLink path; root-directory-relative paths remain unsupported |
| `NtQueryDirectoryFile` | 207 | PARTIAL | synthetic XBE tests FileDirectoryInformation (1) and FileNamesInformation (12); host-backed FileIndex is zero |
| `NtDeviceIoControlFile` | 196 | PARTIAL | synchronous DeviceIoControl pass-through, explicit Win32-to-NTSTATUS mapping, and IOSB failure verification; APC completion unsupported |
| `NtFsControlFile` | 200 | PARTIAL | synchronous native host NtFsControlFile pass-through verified with FSCTL_GET_COMPRESSION; APC completion unsupported |
| `IoCreateSymbolicLink` | 67 | PARTIAL | synthetic XBE import-thunk call plus x86/x64 tests; case-insensitive prefix namespace, duplicate detection, bounded cycle rejection, and path translation |
| `IoDeleteSymbolicLink` | 69 | PARTIAL | synthetic XBE import-thunk call plus x86/x64 tests; deletion and missing-name status |
| `HalRegisterShutdownNotification` | 47 | PARTIAL | synthetic XBE import-thunk call verifies shutdown callback marker; x86/x64 tests cover ordered callbacks, unregister, and one-shot dispatch |

## Coordination boundaries

- Developer 2 owns memory APIs and shared object/handle ownership. This branch does not replace `NtDuplicateObject` or `NtClose`.
- Developer 3 owns threads, synchronization, and I/O completion objects (191, 212, 223, 227). This branch does not duplicate those handlers and does not claim APC or overlapped file-I/O behavior; async NtReadFile/NtWriteFile integration awaits the agreed completion contract.
- NtDeviceIoControlFile is synchronous via host DeviceIoControl; NtFsControlFile is synchronous via the host NTDLL filesystem-control syscall. Both reject non-null guest APC routines with STATUS_NOT_SUPPORTED. They are not a complete Xbox IRP/device stack.

## Real guest-call evidence

`tests/kernel_guest_backend_smoke.cpp` constructs a synthetic x86 XBE with a kernel thunk table for ordinals 256, 67, 69, 47, 190, 207, 187, 196, and 200. The guest calls the resolved imports using the x86 stack ABI. It verifies PsQueryStatistics output; symbolic-link create/collision/delete behavior and path translation through NtCreateFile; directory enumeration with FileNamesInformation and FileDirectoryInformation; NtDeviceIoControlFile failure/IOSB propagation for a disk IOCTL on a directory; NtFsControlFile FSCTL_GET_COMPRESSION on a file; NtClose handle cleanup; and a shutdown callback marker after `XboxKernelShutdownTitle`. This is an import-thunk integration test, not a claim that a retail game XBE has been tested.
