# NV2A / D3D translation integration

XboxRuntime now pins the upstream [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
source as a Git submodule at `third_party/xboxrecomp`. The pin is recorded in
`.gitmodules` and the Git tree; CI checks it out recursively.

## What is integrated

- `xbox_nv2a`: NV2A register state and read/write handlers, PGRAPH method
  dispatch, and the upstream MMIO/push-buffer implementation.
- `xbox_d3d8`: Xbox D3D8 compatibility layer backed by D3D11 on Windows.
- `nv2a.dll`: XboxRuntime module wrapper exposing lifecycle, register access,
  PGRAPH method submission, flush, and translator statistics.

The wrapper allocates one contiguous 65 MiB host block: 64 MiB for Xbox VRAM
and 1 MiB for RAMIN. This mirrors the guest GPU memory model; it does not mean
the Windows PC is restricted to 65 MiB of RAM.

## Build

Clone with submodules:

```powershell
git clone --recurse-submodules https://github.com/rianalwis0v0-a11y/XboxRuntime.git
cmake -S . -B build -A x64
cmake --build build --config Release --target nv2a
```

The output is `build/out/nv2a.dll`. GitHub Actions performs this build on a
Windows x64 runner and publishes the DLL as an artifact.

## ABI

The DLL exports `ModuleStart` and `ModuleStop` for the existing module
manager, plus:

- `XboxNV2A_ReadRegister(offset, size)`
- `XboxNV2A_WriteRegister(offset, value, size)`
- `XboxNV2A_SubmitMethod(subchannel, method, parameter)`
- `XboxNV2A_Flush()`
- `XboxNV2A_GetStats(output)`

- `XboxNV2A_ReadVRAM(offset, destination, length)` / `WriteVRAM(offset, source, length)`
- `XboxNV2A_ReadRAMIN(offset, destination, length)` / `WriteRAMIN(offset, source, length)`
- `XboxNV2A_SubmitMethods(subchannel, pairs, pair_count)` for validated
  interleaved method/parameter dword pairs. The full batch is validated before
  any method is dispatched.


GPU-memory functions use byte offsets into the 64 MiB VRAM and 1 MiB
RAMIN allocations. They reject out-of-range spans and never accept guest or
host pointers as offsets. The batch API accepts at most 1,048,576 method pairs
per call and rejects invalid subchannels, unaligned methods, and out-of-range
method offsets before dispatch.

Register offsets are relative to the NV2A MMIO aperture at guest address
`0xFD000000`; pass the register-block-relative offset, not the full guest VA.

## Not yet complete

This is a real upstream hardware/graphics subsystem integration, not a claim
that XboxRuntime can boot every untouched XBE. The current wrapper does not
yet connect the XBE guest address space to the upstream VEH MMIO/VRAM fault
hooks, schedule PFIFO DMA from the guest's memory map, or provide the full
Xbox D3D device creation path to a running recompiled title. Those are the
next integration tasks. The upstream toolkit is a static-recompilation
runtime; it does not interpret arbitrary original 32-bit XBE machine code.

The upstream NV2A code currently has no public GPU-state destroy/reset API.
For that reason the wrapper retains its backing allocation until the host
process exits rather than leaving dangling pointers during module shutdown.

## Upstream source and licensing

The submodule is pinned to upstream commit
`343b6d8901723b37ce568b2418c5df4e1fe1d223` (xboxrecomp v0.17.0 at the time
of integration). Keep the submodule and its own `LICENSE`, `NOTICE`, and
`LICENSES/` files intact. In particular, the upstream NOTICE identifies
xemu-derived NV2A files under LGPL-2.1-or-later. Do not strip those notices
when distributing modified builds.


## NV4-style pushbuffer submission

`XboxNV2A_SubmitPushBuffer(vram_offset, byte_length, methods_submitted)` parses a bounded
little-endian command stream already stored in the module's 64 MiB VRAM aperture.
It currently supports the NV4-style incrementing-method (type `000`) and
non-incrementing-method (type `010`) packet forms. The full three-bit command type
is checked, so reserved high-bit command types cannot be mistaken for method packets. Packet counts, subchannels, method
alignment/ranges, stream bounds, and the total method budget are checked. The
complete stream is validated before any PGRAPH method is dispatched, preventing
a malformed trailing packet from leaving a partially submitted buffer.

Jump, call, return, long non-incrementing, and other command forms are rejected
rather than guessed. This export is a bounded packet decoder, not yet a complete
PFIFO DMA pusher: it does not resolve DMA objects, follow ring-buffer GET/PUT,
execute subroutine flow, or implement PFIFO interrupts.


## RAMHT lookup and DMA objects

`XboxNV2A_LookupRAMHT(channel, handle, context_out)` implements the NV2A RAMHT
hash/probe lookup using the PFIFO RAMHT base, table-size and search-count fields.
It validates the configured table against the 1 MiB RAMIN aperture, checks the
valid bit and channel ID, and returns the matching context word.

`XboxNV2A_ResolveDMAHandle(channel, handle, output)` resolves the RAMHT context
to a 16-byte-aligned RAMIN DMA object and decodes its class, target, engine,
base and limit. The current mapper accepts the common DMA classes (read, write,
read/write) and linear local-VRAM target only. It rejects unsupported targets,
invalid classes, truncated RAMIN objects and VRAM ranges that exceed the 64 MiB
aperture. The reported mapped span follows the upstream NV2A DMA helper's
`limit` contract.

This is the object lookup/mapping layer only. It does not yet implement PCI/AGP
DMA, page tables, DMA pusher GET/PUT processing, jump/call/return flow, or
channel switching and puller scheduling.


## NV4-style DMA pusher

`XboxNV2A_SubmitDMAPushBuffer(channel, handle, get, put, new_get, methods_submitted)`
resolves a readable DMA object through RAMHT and processes its NV4-style command
stream. GET/PUT are byte offsets within that object. The implementation handles
incrementing and non-incrementing method packets, legacy and NV1A jump commands,
single-level CALL/RETURN, and the reference-counter method. It validates
all reads and control-flow targets against the mapped DMA span, bounds execution
to one million command/data words, and performs a validation pass before GPU
dispatch so malformed streams do not partially execute.

The puller currently binds only the graphics engine. It does not enable the later-generation NOP method on NV2A. Object-bind method 0 and
the RAMHT-handle method range 0x180..0x1fc are resolved through RAMHT; other
unimplemented puller special methods, nested calls, PCI/AGP DMA, DMA page
translation, interrupts, scheduling/channel switching, and IB mode remain
unsupported. This is a functional NV4-style software pusher path, not a full
hardware PFIFO implementation.


## Additional Kelvin fixed-function state mappings

The bridge now maps a tested subset of additional NV097 methods into the
D3D8 compatibility device's render states, which the Windows backend applies
through D3D11. Covered state includes alpha/depth comparisons and masks,
stencil enable/function/reference/masks/operations, blend equation, fog enable,
color and mode, lighting enable, shade mode, and point size. Compare functions,
stencil operations and blend/fog enumerations are translated explicitly rather
than copied as incompatible numeric values.

The mapping function is separately exported and smoke-tested without requiring
a graphics device. Runtime application occurs only when the D3D8 compatibility
device is available. Surface format/offset and render-target binding, texture
sampling, vertex array formats/data, shader microcode, combiners, and broad
method coverage are still separate PGRAPH tasks.
