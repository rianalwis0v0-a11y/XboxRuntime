# XBE loader foundation

The module validates an XBEH header, section table and file ranges; decodes the
standard entry-point XOR field; reserves the image at its declared 32-bit guest
base; copies headers and sections; zero-fills virtual tails; and applies section
page protections.

## Exports
- `ModuleStart()` / `ModuleStop()`: module lifecycle.
- `XbeLoadImage(path)`: validate and map one XBE; returns 0 on success.
- `XbeUnloadImage()`: release the current mapping.
- `XbeGetImageInfo(...)`: retrieve image base, size, decoded entry point and
  section count.
- `LoadXbe(path)`: compatibility launch entry point. It maps the image, then
  returns error 6 (`XBE_E_NO_CPU`) rather than pretending it launched.

## Not implemented yet
Retail executable decryption, kernel import/thunk resolution, TLS setup,
Xbox process/thread state, hardware aliases, and x86 guest instruction
execution are not implemented. The x64 host never calls the guest entry point
as a native function pointer. An x86 interpreter/JIT and Xbox kernel ABI are
required before an original game can run. Images are not silently relocated:
if the declared guest base cannot be reserved, loading fails.
