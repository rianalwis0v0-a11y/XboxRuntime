# XDVDFS read-only disc filesystem

The `xdvdfs.dll` module reads original Xbox XDVDFS images without mounting them as a Windows filesystem.

## Supported operations

- Open a raw XISO image or an original Xbox XGD1 image with the game partition at `0x18300000`.
- Validate the 2048-byte volume descriptor at sector 32, including both `MICROSOFT*XBOX*MEDIA` signatures.
- Parse the root directory and nested directory tables.
- Walk each directory's serialized binary-search-tree entries with bounds and cycle checks.
- Look up paths case-insensitively.
- Read file contents by offset; reads are clamped to the file's declared size.
- Read-only: no file creation, deletion, or disc-image modification.

## C API

Include `modules/xdvdfs/xdvdfs.h`.

1. Call `ModuleStart()`.
2. Call `XDVDFS_OpenImage(path, &volumeInfo)`.
3. Use `XDVDFS_GetFileInfo(path, &fileInfo)` and `XDVDFS_ReadFile(path, offset, buffer, length, &bytesRead)`.
4. Call `XDVDFS_CloseImage()`, then `ModuleStop()`.

Paths are relative to the disc root, for example `default.xbe` or `media\default.xbe`.

## Known limitations

- This is a host-side image reader, not yet a guest kernel optical-drive implementation.
- Physical DVD drive sector access is not implemented.
- XGD2/XGD3 Xbox 360 layouts are intentionally not treated as original Xbox media.
- Filename decoding currently preserves the on-disc single-byte bytes; it does not perform full OEM code-page conversion.
- A malformed image is rejected; no repair or recovery mode is provided.

## Format references

- XDVDFS volume descriptor: sector 32, 2048-byte sectors, magic at offsets `0x000` and `0x7EC`.
- Directory entries are a binary tree with child offsets in DWORD units and variable-length names padded to DWORD alignment.
