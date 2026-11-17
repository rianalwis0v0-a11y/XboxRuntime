# Kernel filesystem providers

FATX and XDVDFS register with the shared dispatcher in guest32/kernel_filesystem_bridge.h. The bridge owns longest-prefix mount routing and file-handle lifetime. The providers do not implement a second dispatch system.

## Registration

The provider registration APIs accept both the NT-visible mount path and the host backing path:

- FATX_RegisterFilesystemProvider(deviceName, backingDevicePath, partitionOffset, partitionLength, &mount)
- XDVDFS_RegisterFilesystemProvider(deviceName, imagePath, &mount)

Example NT mount names:

- FATX: \\Device\\Harddisk0\\Partition1 (writable mount flags 0)
- XDVDFS: \\Device\\Cdrom0 (XBOX_FS_MOUNT_READ_ONLY)

The backing path is supplied by the host storage layer, such as a Windows physical-drive path or XISO image path. FATX partition offsets and lengths are relative to that backing device; they must come from actual partition discovery, not guessed defaults. The kernel storage initialization code should call the relevant registration function when the backing device is ready.

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

The current kernel NtCreateFile, NtReadFile, NtWriteFile, query and close paths in guest32/kernel_compat.cpp already dispatch mounted device paths through XboxFs*. This provider integration preserves that syscall dispatch and links the provider implementations beside the bridge. The storage layer still needs to call the registration functions with real backing paths and partition geometry.

## Regression fixture

tests/kernel_filesystem_provider_smoke.cpp builds two synthetic images:

- 64 KiB FATX volume, 512-byte sectors, two sectors per 1024-byte cluster, one 1500-byte TEST.BIN file spanning two clusters.
- 64-sector XISO image with the XDVDFS descriptor at sector 32, a root directory at sector 40 and one 8-byte DISC.BIN file at sector 50.

The bridge-level test mounts both providers, opens and queries files, reads data, verifies FATX single- and cross-cluster writes with readback, rejects a FATX write past EOF, confirms an open FATX handle prevents unmount, checks XDVDFS writes are denied, closes handles, and unmounts cleanly.
