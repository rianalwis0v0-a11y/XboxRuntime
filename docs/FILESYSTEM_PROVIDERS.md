# Kernel filesystem providers

FATX and XDVDFS register with the shared dispatcher in guest32/kernel_filesystem_bridge.h. The bridge owns longest-prefix mount routing and file-handle lifetime. The providers do not implement a second dispatch system.

## Registration

The provider registration APIs accept both the NT-visible mount path and the host backing path:

- FATX_RegisterFilesystemProvider(deviceName, backingDevicePath, partitionOffset, partitionLength, &mount)
- XDVDFS_RegisterFilesystemProvider(deviceName, imagePath, &mount)

Example NT mount names:

- FATX: \\Device\\Harddisk0\\Partition1 (writable mount flags 0)
- XDVDFS: \\Device\\Cdrom0 (XBOX_FS_MOUNT_READ_ONLY)

The backing path is supplied by the host storage layer, such as a Windows physical-drive path or XISO image path. FATX partition offsets and lengths are relative to that backing device; they must come from actual partition discovery, not guessed defaults. The current repository does not yet contain a working `xbox_storage.dll` implementation or a physical-device/partition enumerator, so registration is an explicit integration API today—not an automatically mounted device. The storage layer must call the relevant registration function when the backing device is ready.

Always close every XBOX_FS_FILE through XboxFsClose before unregistering a mount. The bridge rejects unmount while provider handles are open. If unmount returns STATUS_DEVICE_BUSY, close outstanding handles and retry.

## Implemented callbacks

| Callback | FATX | XDVDFS |
|---|---|---|
| Open | Existing regular files; rejects directories and invalid names | Existing regular files; rejects directories and invalid names |
| Read | Offset reads across FAT cluster chains | Offset reads from XISO extents |
| Write | Bounded in-place writes within the existing file size, including cluster-boundary crossings | Returns STATUS_MEDIA_WRITE_PROTECTED; mount is read-only |
| QueryInformation | EOF size, cluster-rounded allocation size, attributes | EOF size, 2048-byte-sector-rounded allocation size, attributes |
| Close | Releases provider-owned path handle | Releases provider-owned path handle |

## Not implemented yet

FATX currently does not create files, extend or truncate files, allocate/free clusters, rename/delete files, or mutate directories. Writes are deliberately limited to existing file extents so no FAT chain or directory metadata is changed. XDVDFS is read-only by design.

The current kernel NtCreateFile, NtReadFile, NtWriteFile, query and close paths in guest32/kernel_compat.cpp already dispatch mounted device paths through XboxFs*. This provider integration preserves that syscall dispatch and links the provider implementations beside the bridge. The host storage module now discovers and configures attached FATX partitions and XDVDFS optical media before guest startup; explicit image configuration remains available as an override.

## Regression fixture

tests/kernel_filesystem_provider_smoke.cpp builds two synthetic images:

- 64 KiB FATX volume, 512-byte sectors, two sectors per 1024-byte cluster, one 1500-byte TEST.BIN file spanning two clusters.
- 64-sector XISO image with the XDVDFS descriptor at sector 32, a root directory at sector 40 and one 8-byte DISC.BIN file at sector 50.

The bridge-level test mounts both providers, opens and queries files, reads data, verifies FATX single- and cross-cluster writes with readback, rejects a FATX write past EOF, confirms an open FATX handle prevents unmount, checks XDVDFS writes are denied, rejects a second XDVDFS mount, closes handles, and unmounts cleanly. The kernel I/O Group A smoke test also exercises FATX through `K_IoCreateFile`/`NtCreateFile`, `NtReadFile`, `NtWriteFile`, `NtQueryInformationFile`, and `NtClose`.


## Runtime storage configuration

The runtime now includes the host-side `xbox_storage.dll` module and an in-guest mount initializer. The host module exports `XboxStorageSetFatxImage`, `XboxStorageSetXdvdfsImage`, `XboxStorageClearConfiguration`, and `XboxStorageDiscoverAndConfigure`. `main.dll` calls automatic discovery after starting the modules and before launching the guest; the configured environment is inherited by `xbe_guest32.exe`. A launcher may also call the explicit setters before `MainStart` to choose image files or a specific device.

Alternatively, configure these environment variables before starting XboxRuntime:

- `XBOX_RUNTIME_FATX_IMAGE`: FATX partition image or readable/writable raw disk path.
- `XBOX_RUNTIME_FATX_DEVICE`: optional NT device name; defaults to `\\Device\\Harddisk0\\Partition1`.
- `XBOX_RUNTIME_FATX_OFFSET`: byte offset of the FATX partition; defaults to zero (a standalone partition image).
- `XBOX_RUNTIME_FATX_LENGTH`: partition byte length; for ordinary image files it may be omitted and is derived from file size minus offset. For raw devices, supply an explicit length.
- `XBOX_RUNTIME_XDVDFS_IMAGE`: XISO image path.
- `XBOX_RUNTIME_XDVDFS_DEVICE`: optional NT device name; defaults to `\\Device\\CdRom0`.

The guest validates the configured ranges, registers providers inside the guest process (where the shared filesystem bridge actually lives), and unregisters mounts during orderly shutdown. If a mount fails, earlier mounts from that configuration are rolled back. The main runtime now scans physical disks and mounted optical drives automatically. Auto-discovered FATX volumes are mounted read-only. Explicit `XboxStorageSetFatxImage` configuration remains writable and should only be used with media that is safe to modify. The XDVDFS image is read-only.


## Automatic Windows storage discovery

The host storage DLL exports `XboxStorageDiscoverAndConfigure(&count)`. Call it before launching the guest. It scans Windows physical drives 0–31 using `IOCTL_DISK_GET_DRIVE_LAYOUT_EX`, checks partition start/length bounds, and accepts only partitions with a FATX signature and plausible cluster geometry. It also checks mounted optical drive letters for the XDVDFS volume descriptor at sector 32. Discovered NT names use `\\Device\\HarddiskN\\PartitionM` and `\\Device\\CdRom0`.

**Safety:** physical disks are opened with `GENERIC_READ` during discovery. Auto-discovered FATX partitions are mounted read-only, and writes return `STATUS_MEDIA_WRITE_PROTECTED`. The existing explicit `XboxStorageSetFatxImage` API remains writable for a caller-selected image/device. Discovery configures up to 16 FATX partitions and one XDVDFS optical disc in this version. It does not format, repair, or modify disks.

The optical raw-volume path is passed to the XDVDFS parser after its descriptor is verified. The parser uses Win32 file handles and disk-length IOCTLs so raw optical devices and ordinary XISO image files share the same read path. Drive letters must already be assigned by Windows; unmounted optical devices are not yet enumerated independently.
