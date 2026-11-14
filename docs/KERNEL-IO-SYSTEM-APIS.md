# Xbox kernel I/O and system services

Pinned ABI source: XboxDev/nxdk revision `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`, especially `lib/xboxkrnl/xboxkrnl.h`, `xboxdef.h`, and `xboxkrnl.exe.def`.

## Initial audit

The export catalogue is an ordinal inventory, not an implementation ledger. Existing guest dispatch already routes a limited file-I/O subset: `NtCreateFile` (190), `NtOpenFile` (202), `NtReadFile` (219), `NtWriteFile` (236), `NtFlushBuffersFile` (198), `NtDeleteFile` (195), `NtQueryDirectoryFile` (207), `NtQueryInformationFile` (211), `NtSetInformationFile` (226), `NtQueryVolumeInformationFile` (218), and `NtQueryFullAttributesFile` (210). The directory query currently supports only `FileDirectoryInformation` (class 1); file information queries support only a small subset of classes. The current read/write handlers complete synchronously and do not implement APC/overlapped completion. `IoCreateSymbolicLink`/`IoDeleteSymbolicLink` now have a bounded guest namespace map used by the existing file-path translator; this is not a complete NT object manager or device-object implementation.

The following catalogue groups remain undispatched: most IRP/device-object APIs (ordinals 59-87), `NtDeviceIoControlFile` (196), `NtFsControlFile` (200), `NtReadFileScatter` (220), `NtWriteFileGather` (237), and I/O completion exports (191, 223, 227). These remain audit findings, not assertions that every API should be implemented by this tranche.

System-information candidates include `PsQueryStatistics` (256) and `MmQueryStatistics` (181). HAL shutdown notifications are registered through `HalRegisterShutdownNotification` (47). The latter overlaps the memory developer's ownership and is intentionally excluded here. `PsQueryStatistics` is the first isolated implementation in this branch.

## Implemented in this branch

| API | Ordinal | Status | Evidence |
|---|---:|---|---|
| `PsQueryStatistics` | 256 | PARTIAL | x86 guest stdcall smoke; returns process-backing thread/handle counts, not a console-wide Xbox object-manager snapshot |
| `IoCreateSymbolicLink` | 67 | PARTIAL | x86 guest stdcall smoke; case-insensitive prefix namespace, duplicate detection, bounded cycle rejection, and path translation |
| `IoDeleteSymbolicLink` | 69 | PARTIAL | x86 guest stdcall smoke; deletion and missing-name status |
| `HalRegisterShutdownNotification` | 47 | PARTIAL | x86 guest and x64 host tests; ordered callbacks, unregister, one-shot shutdown dispatch |

## Coordination boundaries

- Developer 2 owns memory APIs and shared object/handle ownership. This branch does not replace `NtDuplicateObject` or `NtClose`.
- Developer 3 owns threads, synchronization, and asynchronous completion. This branch does not claim APC or overlapped file-I/O behavior.
- File/device I/O extensions must use the pinned NXDK layouts and return explicit unsupported/error statuses until their host backing semantics are implemented.
