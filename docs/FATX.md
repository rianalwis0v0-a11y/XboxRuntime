# FATX read-only filesystem module

The FATX module opens a FATX volume from a Windows file or device path and a caller-supplied partition range. It does not modify the volume.

## Supported operations

- Validate the FATX volume header and supported sectors-per-cluster values (1 through 1024 sectors, powers of two supported by libfatx) with both the XboxRuntime bounds-checked parser and vendored XboxDev/libfatx.
- Derive the cluster size, FAT entry width, FAT size, data offset, and usable cluster count from the supplied partition length.
- Read FAT16- or FAT32-width cluster chains.
- Resolve case-insensitive paths and list directory entries; FATX single-byte filenames are decoded as OEM CP437.
- Read file data by byte offset across cluster boundaries.
- Reject out-of-range partitions, invalid cluster references, reserved/bad FAT markers, and cyclic directory/file cluster chains.

## C API

Include `modules/fatx/include/fatx.h`.

1. Call `ModuleStart()`.
2. Call `FATX_OpenVolumeW(devicePath, partitionOffset, partitionLength, &volume)`. A zero partition length means the remaining available device/file length.
3. Use `FATX_GetInfo`, `FATX_ListDirectoryW`, and `FATX_ReadFileW`.
4. Call `FATX_CloseVolume(volume)`, then `ModuleStop()`.

The API is read-only. The vendored libfatx device open is patched to use `rb`; the runtime does not expose its write/format functions. The API does not create, rename, delete, or write files, and it does not mount a Windows drive letter.

## Limitations

- This is currently a host-side volume reader, not a guest Xbox kernel filesystem driver.
- OEM filename decoding currently targets CP437; unusual non-CP437 Xbox locale/code-page variants are not yet handled.
- No write support, journaling, repair, or recovery mode is provided.
- Callers must not close a volume concurrently with operations using that handle.

## Test coverage

The Windows regression test builds synthetic FATX volumes and checks metadata, root listing, multi-cluster reads, offset reads crossing a cluster boundary, partition bounds, cyclic-chain rejection, strict end-of-chain marker handling (only `0xFFFF`/`0xFFFFFFFF`), and the 32-bit FAT geometry threshold. The module cross-checks computed cluster/FAT geometry against XboxDev/libfatx during mount. CI builds and links the vendored XboxDev/libfatx C library before running the module test. This does not yet constitute validation against a complete retail Xbox HDD image.


## FATX write support (initial image-only milestone)

The legacy `FATX_OpenVolumeW` API remains read-only. The opt-in
`FATX_OpenImageReadWriteW` API is restricted to ordinary drive-letter image
file paths; it rejects device-namespace paths so this initial write path cannot
write directly to a physical Xbox HDD.

`FATX_WriteFileW` currently supports overwriting bytes in an existing
non-empty file and appending at the current EOF, including allocation of
additional clusters. Each write is limited to the signed upstream offset range
(ending at or before `INT_MAX`, currently about 2 GiB). It validates the existing cluster chain for cycles,
bounds, and invalid FAT markers before calling the vendored writer, serializes
operations per volume, flushes FAT metadata and file data, and reports the
number of bytes written.

Not yet supported by this first write milestone: creating empty files,
sparse writes beyond EOF, file/directory creation, delete, rename, truncate,
format, repair, or direct physical-device writes. FATX has no journal, so a
power loss or I/O failure during a multi-step update can still leave an image
inconsistent. Always work on a backup copy.
