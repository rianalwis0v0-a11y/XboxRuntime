# Xbox kernel I/O and system services

Pinned ABI source: XboxDev/nxdk revision `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`, especially `lib/xboxkrnl/xboxkrnl.h`, `xboxdef.h`, and `xboxkrnl.exe.def`.

## Initial audit

The export catalogue is an ordinal inventory, not an implementation ledger. Existing guest dispatch already routes a limited file-I/O subset: `NtCreateFile` (190), `NtOpenFile` (202), `NtReadFile` (219), `NtWriteFile` (236), `NtFlushBuffersFile` (198), `NtDeleteFile` (195), `NtQueryDirectoryFile` (207), `NtQueryInformationFile` (211), `NtSetInformationFile` (226), `NtQueryVolumeInformationFile` (218), and `NtQueryFullAttributesFile` (210). The directory query currently supports only `FileDirectoryInformation` (class 1); file information queries support only a small subset of classes. The current read/write handlers complete synchronously and do not implement APC/overlapped completion.

The following catalogue groups are not dispatched by the inspected baseline: most IRP/device-object APIs (ordinals 59-87), `NtDeviceIoControlFile` (196), `NtFsControlFile` (200), `NtReadFileScatter` (220), `NtWriteFileGather` (237), `NtCreateIoCompletion` (191), `NtSetIoCompletion` (227), `NtRemoveIoCompletion` (223), `IoCreateSymbolicLink` (67), and `IoDeleteSymbolicLink` (69). These are audit findings, not assertions that every API should be implemented by this tranche.

System-information candidates include `PsQueryStatistics` (256) and `MmQueryStatistics` (181). The latter overlaps the memory developer's ownership and is intentionally excluded here. `PsQueryStatistics` is the first isolated implementation in this branch.

## Implemented in this branch

| API | Ordinal | Status | Evidence |
|---|---:|---|---|
| `PsQueryStatistics` | 256 | PARTIAL | x86 guest stdcall smoke; returns process-backing thread/handle counts, not a console-wide Xbox object-manager snapshot |

## Coordination boundaries

- Developer 2 owns memory APIs and shared object/handle ownership. This branch does not replace `NtDuplicateObject` or `NtClose`.
- Developer 3 owns threads, synchronization, and asynchronous completion. This branch does not claim APC or overlapped file-I/O behavior.
- File/device I/O extensions must use the pinned NXDK layouts and return explicit unsupported/error statuses until their host backing semantics are implemented.
