# FATX read-only filesystem module

The FATX module opens a FATX volume from a Windows file or device path and a caller-supplied partition range. It does not modify the volume.

## Supported operations

- Validate the FATX volume header and supported sectors-per-cluster values.
- Derive the cluster size, FAT entry width, FAT size, data offset, and usable cluster count from the supplied partition length.
- Read FAT16- or FAT32-width cluster chains.
- Resolve case-insensitive paths and list directory entries.
- Read file data by byte offset across cluster boundaries.
- Reject out-of-range partitions, invalid cluster references, reserved/bad FAT markers, and cyclic directory/file cluster chains.

## C API

Include `modules/fatx/include/fatx.h`.

1. Call `ModuleStart()`.
2. Call `FATX_OpenVolumeW(devicePath, partitionOffset, partitionLength, &volume)`. A zero partition length means the remaining available device/file length.
3. Use `FATX_GetInfo`, `FATX_ListDirectoryW`, and `FATX_ReadFileW`.
4. Call `FATX_CloseVolume(volume)`, then `ModuleStop()`.

The API is read-only. It does not create, rename, delete, or write files, and it does not mount a Windows drive letter.

## Limitations

- This is currently a host-side volume reader, not a guest Xbox kernel filesystem driver.
- FATX directory names are currently widened from their stored single-byte values; full OEM code-page conversion is not implemented.
- No write support, journaling, repair, or recovery mode is provided.
- Callers must not close a volume concurrently with operations using that handle.

## Test coverage

The Windows regression test builds a synthetic FATX volume and checks metadata, root listing, multi-cluster reads, offset reads crossing a cluster boundary, partition bounds, and cyclic-chain rejection.
