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


## NV097 method-offset compatibility

The pinned PGRAPH translator contains several legacy method constants that do
not match the NV2A register definitions. The bridge now normalizes the actual
NV097 color-clear, clear-surface, clear-rectangle, depth-enable, and cull-enable
methods to the translator's expected aliases. It deliberately handles actual
depth-function, cull-face, shade-mode, and stencil-reference methods itself,
because those offsets collide with the translator's incorrect aliases. The
actual PGRAPH register file is still updated using the guest method offset.

This prevents depth-function writes from being mistaken for depth-test enable,
cull-face writes from being mistaken for cull enable, and stencil-reference
writes from being mistaken for shade mode. Front/back cull mode, depth compare,
shade mode, and stencil reference use the D3D8/D3D11 compatibility device when
available.


## Linear render-surface decoding

`XboxNV2A_DecodeSurface` decodes the current NV097 surface-format, pitch,
color/depth offsets, and clip dimensions. It maps the common 16-bit and 32-bit
color formats plus Z16/Z24S8 to the D3D8 compatibility formats, checks row
pitches, and proves the final color/depth byte spans stay inside the 64 MiB
VRAM aperture. The first implementation intentionally rejects swizzled and
multisampled render surfaces instead of treating them as linear memory.


## Render-target binding and VRAM synchronization

When a game writes NV097 surface format, pitch, color/depth offsets, and clip
dimensions, the bridge records those registers. Before a clear or draw-begin
method, it decodes the descriptor, creates a lockable D3D8 compatibility render
target (and optional depth surface), uploads the guest color-buffer rows from
VRAM, and binds the target. The compatibility layer's D3D11 implementation
therefore receives the actual guest render target instead of always drawing
into the host default target.

On target changes, the previous color target is read back to its guest VRAM
span before the replacement target is uploaded. `XboxNV2A_Flush` and module
shutdown also read the active color target back into VRAM. Depth surfaces are
created and bound but are not yet synchronized back to guest depth memory.
This path currently supports linear single-sample surfaces only; swizzled,
multisampled, tiled, and unsupported color/depth formats remain rejected.


## PFIFO CACHE1 DMA_PUT integration

The bridge now also accepts a PFIFO DMA instance directly, using the RAMIN
instance address from `CACHE1_DMA_INSTANCE`. When `CACHE1_PUSH1` selects DMA
mode, `CACHE1_DMA_PUSH_ACCESS` is enabled, and the guest writes
`CACHE1_DMA_PUT`, the bridge resolves the DMA object, executes the bounded
GET-to-PUT command stream, and advances `CACHE1_DMA_GET` only after successful
validation and dispatch. PIO mode and disabled DMA push access are left alone.

This connects the software pusher to the guest-visible PFIFO MMIO path. Ring
wraparound, DMA fetch caching, push-buffer interrupts/error recovery, channel
scheduling, and full PFIFO timing/flow semantics are still not implemented.


## Per-subchannel context-DMA bindings

NV097 object methods in the 0x180..0x1fc range are now retained as resolved
per-channel/per-subchannel DMA contexts after RAMHT lookup. The bridge exposes
`XboxNV2A_GetDMAContext` so vertex, texture, surface, semaphore, and report
translation paths can retrieve the actual mapped object base and limit rather
than only seeing the raw guest handle. The first context slot is covered by the
smoke test; the remaining slots share the same validated storage path.


## Bounded vertex-array fetch

`XboxNV2A_ReadVertexArray` now reads guest vertex elements through the bound
`SET_CONTEXT_DMA_VERTEX_A` or `VERTEX_B` object. It reads the NV097 array
offset/format registers, supports UB_D3D, UB_OGL, S1, S32K, and float component
encodings, honors per-array stride, and checks both DMA-object and VRAM bounds
before copying packed elements. Compressed vertex format and conversion to a
host vertex declaration are not included yet; this is the validated guest-data
fetch stage for a later DRAW_ARRAYS translator.


## Host FVF vertex stream generation

`XboxNV2A_BuildVertexStream` combines the guest float3 position array with
optional UB_D3D diffuse color and float2 TEX0 arrays into a D3D8 FVF-compatible
vertex stream (`XYZ | DIFFUSE | TEX1`). Missing color defaults to opaque white
and missing TEX0 defaults to (0,0). The smoke test checks two vertices, their
colors/texture coordinates, the 24-byte host stride, and destination-capacity
rejection. This prepares a bounded input stream for a subsequent DRAW_ARRAYS
submission path; it does not yet cover normals, weights, specular, additional
texture sets, compressed formats, or shader-driven vertex layouts.


## DRAW_ARRAYS submission

The bridge tracks the current per-channel/subchannel BEGIN_END primitive mode
and routes NV097 DRAW_ARRAYS through the bounded FVF builder into the D3D8
compatibility device's `SetVertexShader(FVF)` and `DrawPrimitiveUP` methods.
Points, lines, line strips/loops, triangles, triangle strips/fans, polygons,
quads, and quad strips are mapped; line loops and quad topologies are expanded
into supported D3D primitive lists. Successful custom draws are included in
the bridge's draw-call and submitted-vertex statistics.

This initial path consumes float3 position plus optional UB_D3D diffuse and
float2 TEX0 arrays. It does not yet translate normals, weights, specular,
additional texture sets, indexed draws, compressed vertex data, or programmable
vertex/pixel shader microcode.


## Texture format decoding

`XboxNV2A_DecodeTextureRGBA8` converts supported NV097 texture payloads to
tightly packed RGBA8 with checked source pitch, source length, destination
capacity, and integer-size bounds. Supported formats are Y8, A8, A1R5G5B5,
X1R5G5B5, A4R4G4B4, R5G6B5, A8R8G8B8, X8R8G8B8, and DXT1, DXT3, and DXT5 (including clipped edge blocks). This is the CPU format-conversion stage only: guest texture DMA
address resolution, mip chains, palette formats, sampler state,
texture object caching, and binding to D3D8 texture stages remain to be wired.


Texture sampler filter methods (`NV097_SET_TEXTURE_FILTER`, per 0x40-byte stage) now map the NV2A minification and magnification selectors to D3D8 point/linear filters and the mip selector to none/point/linear. The supported NV2A convolution selector uses linear filtering as an explicit fallback. Invalid selector combinations are rejected; the mapping is exported as `XboxNV2A_MapTextureFilter` and covered by host smoke tests. This maps sampler state only: mip-level texture allocation and upload are still not implemented.

Texture sampler address methods are now translated per stage for U/V wrapping:
wrap, mirror, clamp, border, and mirror-once map to the matching D3D8
texture-stage states. Per-stage border color is also forwarded. Invalid address
modes and unavailable device state are not silently reported as handled.


## Swizzled texture decode

`XboxNV2A_DecodeSwizzledTextureRGBA8` handles the common power-of-two
Morton/Z-order texture layout for uncompressed Y8, A8, A1R5G5B5, X1R5G5B5,
A4R4G4B4, R5G6B5, A8R8G8B8, and X8R8G8B8 data. It validates dimensions,
source length, destination capacity, and allocation size before unswizzling
through the pinned D3D8 compatibility helper and passing the linear result to
the existing RGBA8 decoder. Linear-image and DXT block formats are explicitly
rejected by this entry point; they use the linear or block decoder. This is
the swizzle conversion stage, not yet DMA-driven texture creation, cache
invalidation, mip-chain upload, or D3D8 texture-stage binding.


The D3D8 backend does not expose the D3D9 constant-blend-factor render state.
NV097 blend color is therefore left explicitly unsupported rather than passing a
D3D9-only state ID into the D3D8 device. Source/destination blend factors and
the blend equation remain mapped where the D3D8 API has a real equivalent.


## Mip-chain decoding

`XboxNV2A_DecodeTextureMipChainRGBA8` validates and decodes up to 16
consecutive mip levels for the supported swizzled uncompressed formats and
DXT1/3/5. It computes each level's source and RGBA8 output span with 64-bit
bounds checks, validates the complete chain before decoding, and returns
per-level output offsets and byte sizes. The test covers a 2x2-to-1x1 swizzled
chain and truncated-source / undersized-destination rejection. This still
does not upload mip levels into a D3D8 texture object or bind them to stages.


## DMA-bound texture mip-chain decoding

`XboxNV2A_DecodeTextureMipChainFromDMA` now joins the context-DMA binding
path to the texture decoder. It reads the selected DMA_A or DMA_B object bound
to the current channel/subchannel, validates the texture offset against that
object's mapped local-VRAM span, and decodes the mip chain without accepting
guest pointers. The smoke test binds a real RAMHT-resolved DMA_A object through
the NV4 pusher, decodes a 2x2-to-1x1 swizzled chain from its VRAM span, and
checks unbound-context and out-of-range failures. D3D8 texture allocation,
upload, cache invalidation, and stage binding remain outstanding.
