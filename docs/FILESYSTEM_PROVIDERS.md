# Kernel filesystem providers

FATX and XDVDFS register with the shared dispatcher in guest32/kernel_filesystem_bridge.h. The bridge owns longest-prefix mount routing and file-handle lifetime. The providers do not implement a second dispatch table.

## Registration

The provider registration APIs accept both the NT-visible mount path and the host backing path:

- FATX_RegisterFilesystemProvider(deviceName, backingDevicePath, partitionOffset, partitionLength, &mount)
- XDVDFS_RegisterFilesystemProvider(deviceName, imagePath, &mount)

Example NT mount names:

- FATX: \\Device\\Harddisk0\\Partition1 (registers writable, mount flags 0)
- XDVDFS: \\Device\\CdRom0 (registers XBOX_FS_MOUNT_READ_ONLY)

The backing path is a host path accepted by the filesystem module, such as a Windows physical-drive path or an image-file path. FATX partition offsets and lengths are relative to that backing device.

Always close every XBOX_FS_FILE through XboxFsClose before unregistering a mount. The bridge rejects unmount while provider handles are open. If unmount returns STATUS_DEVICE_BUSY, close outstanding handles and retry.

## Implemented callbacks

| Callback | FATX | XDVDFS |
|---|---|---|
| Open | Existing regular files; rejects directories and invalid names | Existing regular files; rejects directories and invalid names |
| Read | Offset reads across FAT cluster chains | Offset reads from XISO extents |
| Write | Bounded in-place writes within the existing file size | Returns STATUS_MEDIA_WRITE_PROTECTED |
| QueryInformation | EOF size, cluster-rounded allocation size, attributes | EOF size, 2048-byte-sector-rounded allocation size, attributes |
| Close | Releases provider-owned path handle | Releases provider-owned path handle |

## Not implemented yet

FATX currently does not create files, extend or truncate files, allocate/free clusters, rename/delete files, or mutate directories. Writes are deliberately limited to existing file extents so no FAT chain or directory metadata is changed. XDVDFS is read-only by design.

The guest NtCreateFile, NtReadFile, NtWriteFile, query and close exports still need to be wired by the kernel team to call the shared bridge. This PR provides the filesystem providers and links them beside the bridge; it does not claim that all guest NT I/O dispatch is already active.

## Regression fixture

tests/kernel_filesystem_provider_smoke.cpp builds two synthetic images:

- 64 KiB FATX volume, 512-byte sectors, two sectors per 1024-byte cluster, one 8-byte TEST.BIN file.
- 64-sector XISO image with the XDVDFS descriptor at sector 32, a root directory at sector 40 and one 8-byte DISC.BIN file at sector 50.

The bridge-level test mounts both providers, opens and queries files, reads data, verifies FATX in-place write/readback, confirms an open FATX handle prevents unmount, checks XDVDFS writes are denied, closes handles, and unmounts cleanly.
