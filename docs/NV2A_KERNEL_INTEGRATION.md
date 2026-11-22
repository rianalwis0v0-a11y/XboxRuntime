# NV2A kernel integration status

Branch: `work/nv2a-guest-physical-map`  
Pull request: [#39](https://github.com/rianalwis0v0-a11y/XboxRuntime/pull/39)

This document describes the implementation on this branch as it exists now.
It distinguishes working, CI-tested plumbing from Xbox hardware behavior that
is still incomplete.

## 1. Ownership and process model

The host backend in `modules/xbox_kernel/xbox_kernel.cpp` owns the XBE image
mapping, guest process, GPU RPC pipe, shared NV2A-local memory, and a separate
guest physical-RAM section. The XBE image mapping is only the guest virtual
image; it is never treated as physical RAM.

The guest x86 process maps the same physical-RAM section and initializes the
physical-memory manager. The NV2A worker maps that same section in the host
process and binds it to the provider with
`XboxNV2A_BindGuestPhysicalMemory`. The worker is stopped and joined before
the host mapping is released.

The GPU RPC pipe is independent of the lifecycle/control pipe. It carries
register reads/writes, method submission, flush, and bounded VRAM/RAMIN
transfers. The host validates request framing, widths, payload lengths, and
transfer bounds before dispatch.

## 2. Guest physical RAM and allocation

The current backend creates a dedicated 64 MiB shared guest RAM section at
physical base `0x00000000`. It is separate from both the XBE image mapping and
the NV2A-local 64 MiB VRAM plus 1 MiB RAMIN mapping.

The guest physical-memory manager provides:

- A shared guest view of the host-owned physical-RAM section.
- Bounded guest-virtual-to-physical translation for ranges inside that view.
- Contiguous allocation with minimum/maximum physical-address constraints and
  power-of-two alignment.
- Allocation tracking and free-range coalescing on release.
- Validation of committed guest pages and Windows page protections before
  translation.

The guest memory exports for contiguous allocation/free and physical-address
queries use this manager. The provider accepts PCI target `0x00010000` and
AGP target `0x00020000` only through the explicitly bound guest RAM window.
Local target `0` remains bounded to NV2A VRAM.

**Lifetime limitation:** allocations are not pinned against in-flight GPU DMA.
There is no command-fence tracking that prevents a guest from freeing memory
while the GPU still references it. Callers must not assume full Xbox kernel
memory-manager semantics yet.

## 3. NV2A DMA and command submission

The provider validates each DMA object's mapped length, offset, requested
span, physical base, and bound-window limit before creating a host pointer.
The checked span resolver is used for:

- DMA pushbuffer reads and command walking.
- Vertex-array reads.
- Linear and mip-chain texture reads.
- Palette and paletted-texture reads.

The provider smoke test constructs a RAMHT entry and PCI DMA object, places a
pushbuffer in the shared guest physical-RAM window, submits it, and checks the
updated GET pointer and method count. It also checks invalid physical-window
bounds and unbinding behavior.

This validates provider-side DMA resolution and command parsing. It does not
prove every Xbox DMA mode, DMA writes, full PFIFO scheduling, or complete
hardware timing/interrupt behavior.

## 4. NV2A MMIO and renderer

The guest installs a vectored exception handler for the NV2A MMIO aperture
(`0xFD000000`–`0xFE000000`). The x86 decoder supports the explicit load/store
instruction forms implemented in `guest32/kernel_gpu_mmio.cpp`, then forwards
them over the GPU RPC pipe. Unsupported instruction forms or failed RPCs
continue exception search rather than being silently emulated.

The host worker loads `nv2a.dll`, binds guest RAM, creates a renderer window,
and initializes the D3D8/D3D11 translation path. Renderer initialization is
required for the worker to report the provider as fully ready.

This is a bounded MMIO implementation, not a complete chipset or PCI bus
emulator. PCI configuration-space forwarding, physical device interrupts,
and full Xbox display-mode/flip semantics remain unfinished.

## 5. Framebuffer readback and integration test status

The provider has a render-target-to-VRAM readback path:

1. `XboxNV2A_Flush()` flushes the PGRAPH translator.
2. The provider copies the bound color surface rows into the configured VRAM
   surface span.
3. `XboxNV2A_ReadVRAM` and the guest GPU RPC can read the copied bytes in
   bounded transfers (maximum 64 KiB per RPC).

The CI workflow builds a GPU-only NXDK/pbkit triangle XBE and executes it
through the real loader and kernel backend. The integration harness now obtains
a provider snapshot after the guest GPU pipe closes: draw/vertex counters,
bound surface metadata, and VRAM pixel bytes. It asserts a non-empty draw,
valid surface bounds, and that a center pixel differs from a pixel near the
bottom-right edge. This is an initial end-to-end pixel assertion, not yet a
pixel-perfect reference-image comparison. If the provider cannot bind or
synchronize a surface, the test fails instead of treating a clean guest exit
as rendering success.

## 6. Current CI and remaining work

GPU pixel-test implementation added in commit `3633b5f8b498acbe7651aca0a7210478742353f2`; per-title diagnostic reset added in `f8678d0bf2aaa672c7a365febbb46c8489329af4`.

Both latest GitHub Actions workflows passed:

- Kernel host and guest32 build/tests.
- NV2A provider build and smoke tests.

The workflow also built and executed the GPU-only triangle XBE. The new
pixel-level assertion is now part of the integration harness; its first CI
result is pending on the current branch head.

Remaining major work:

1. Add and pass the end-to-end framebuffer pixel assertion described above.
2. Add DMA allocation pinning/fence-aware free semantics.
3. Complete PCI configuration-space forwarding.
4. Deliver GPU interrupts through the existing guest interrupt manager.
5. Complete guest display-mode, flip, and presentation integration.
6. Expand instruction, DMA-target, and hardware-compatibility coverage.

PR #39 remains a draft until the framebuffer pixel test passes and the
remaining scope is reviewed. Do not describe the NV2A implementation as a
complete Xbox GPU or claim full game compatibility.
