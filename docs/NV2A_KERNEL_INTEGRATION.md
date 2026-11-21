# NV2A integration contract for the combined kernel

Target branch: `work/kernel-combined-clean-import`

This document separates **implemented plumbing** from **unfinished hardware
semantics**. The combined branch now has a guest GPU RPC client, host worker,
shared GPU-memory mapping, a bounded RAMIN instance allocator, an NV2A MMIO
fault decoder, and dedicated guest smoke tests. These are integration
milestones, not proof of complete Xbox GPU behavior. PCI forwarding,
physical-address/DMA translation, device interrupt delivery, and display
presentation remain unfinished.

## 1. Existing architecture and ownership

The combined kernel runs the Xbox guest in a dedicated 32-bit process:

- `modules/xbox_kernel/xbox_kernel.cpp` owns the host-side execution backend,
  process lifetime, shared XBE image mapping, and the host end of the control
  named pipe.
- `modules/xbe_loader/xbe_backend_api.h` defines
  `XBE_EXECUTION_BACKEND_V2`. Its `reserveImage`, `resolveImports`,
  `startGuest`, `stopGuest`, and `releaseImage` callbacks are the supported
  host/backend lifecycle.
- `guest32/guest32.cpp` owns the guest-side process startup and the other end
  of the control pipe.
- `guest32/guest_protocol.h` defines the version-3 control protocol:
  `XboxGuestHello`, `XboxGuestCommandMessage`,
  `XboxGuestCommandReply`, and `XboxGuestExitReply`.
- The XBE image is backed by a named file mapping. The host maps it at the
  requested guest VA in the host process; the guest maps the same backing
  pages at the same 32-bit VA in its own process.

**Do not add a second kernel dispatcher or replace the control protocol.**
GPU RPC must use a separate, dedicated named pipe owned by the existing
`xbox_kernel.dll` backend. The control pipe is synchronous and carries
lifecycle commands; mixing unsolicited GPU replies into it would break the
request/reply framing used by `ResolveImports`, `StartGuest`, and
`XboxKernelWaitForGuestExit`.

### GPU transport wire ABI

The shared header `guest32/kernel_gpu_service_protocol.h` has been added on
this branch. It defines:

- Magic `XBOX_GPU_RPC_MAGIC`, protocol version `XBOX_GPU_RPC_VERSION`.
- A packed 36-byte `XboxGpuRpcRequest`.
- A packed 24-byte `XboxGpuRpcResponse`.
- Monotonic request IDs, fixed-width arguments, and an explicit 64 KiB maximum
  transfer size.
- No process-local pointers or handles on the wire.

The first operation is `XboxGpuRpcPing`; it is intended for a startup
round-trip test. Register access, method submission, flush, VRAM, and RAMIN
operations use the same operation numbers as the RPC work in PR #37.

PR #37 is a useful protocol/service reference, but it is based on the older
`integration/cxbx-reloaded-gpl-port` branch. Its pipe server in
`modules/xbe_loader/xbe_loader.cpp` must be ported into the combined backend;
do not merge PR #37 as a replacement for the combined kernel.

### Lifecycle and threading contract

1. The host backend creates a second pipe named independently from the
   control pipe and passes its name to `xbe_guest32.exe` as
   `--gpu-pipe <name>`.
2. The guest opens that pipe during process initialization and initializes
   its GPU RPC client before reporting its startup hello.
3. The host starts one GPU service worker for that pipe. The worker owns
   request parsing and calls the registered NV2A provider; it must not take
   the backend's global lifecycle mutex while blocking on pipe I/O.
4. The guest client serializes requests on its pipe. The host validates magic,
   version, opcode, request ID, payload length, access width, and all memory
   bounds before dispatch.
5. The host cancels synchronous worker I/O and joins the worker before closing
   the GPU pipe or releasing the guest process/mappings. The guest closes its
   client pipe during process teardown.
6. Provider callbacks run on the GPU worker thread unless the provider
   explicitly supplies its own queue/thread. A provider must document whether
   it is thread-safe. No callback may retain a pointer to a temporary RPC
   payload after the request returns.

A minimal client API for the implementation is:

```cpp
// guest32/kernel_gpu_service.h (implementation to be added)
bool XboxKernelGpuServiceInitialize(HANDLE pipe);
void XboxKernelGpuServiceShutdown();
bool XboxKernelGpuReadRegister(uint32_t offset, uint32_t width,
                               uint64_t* value);
bool XboxKernelGpuWriteRegister(uint32_t offset, uint32_t width,
                                uint64_t value);
bool XboxKernelGpuSubmitMethod(uint32_t subchannel, uint32_t method,
                               uint32_t parameter);
bool XboxKernelGpuFlush();
bool XboxKernelGpuReadVram(uint32_t offset, void* dst, uint32_t bytes);
bool XboxKernelGpuWriteVram(uint32_t offset, const void* src, uint32_t bytes);
bool XboxKernelGpuReadRamin(uint32_t offset, void* dst, uint32_t bytes);
bool XboxKernelGpuWriteRamin(uint32_t offset, const void* src, uint32_t bytes);
```

The host provider should expose the matching operations through a versioned
C ABI. The exports already used by PR #37 are:

```cpp
uint64_t __cdecl XboxNV2A_ReadRegister(uint32_t offset, unsigned int width);
int __cdecl XboxNV2A_WriteRegister(uint32_t offset, uint64_t value,
                                   unsigned int width);
int __cdecl XboxNV2A_SubmitMethod(uint32_t subchannel, uint32_t method,
                                  uint32_t parameter);
void __cdecl XboxNV2A_Flush();
int __cdecl XboxNV2A_ReadVRAM(uint32_t offset, void* dst, uint32_t bytes);
int __cdecl XboxNV2A_WriteVRAM(uint32_t offset, const void* src, uint32_t bytes);
int __cdecl XboxNV2A_ReadRAMIN(uint32_t offset, void* dst, uint32_t bytes);
int __cdecl XboxNV2A_WriteRAMIN(uint32_t offset, const void* src, uint32_t bytes);
```

These are provider signatures from PR #37, not exports currently wired by
`xbox_kernel.dll` on the combined branch. Missing provider exports must return
a deterministic `STATUS_NOT_SUPPORTED`-equivalent result, not silently
succeed.

## 2. GPU memory and DMA

### What exists today

`guest32/kernel_memory.h` declares the Xbox exports:

```cpp
void* __stdcall K_MmClaimGpuInstanceMemory(
    SIZE_T numberOfBytes, SIZE_T* numberOfPaddingBytes);
void* __stdcall K_MmMapIoSpace(
    ULONG_PTR physicalAddress, SIZE_T numberOfBytes, ULONG protect);
void __stdcall K_MmUnmapIoSpace(
    void* baseAddress, SIZE_T numberOfBytes);
```

The current combined branch has a named shared GPU-memory mapping created by
`xbox_kernel.dll` and mapped by `xbe_guest32.exe`. The provider receives the
same host view through `ModuleStartWithMemory`. The shared layout is defined
in `kernel_gpu_service_protocol.h` as VRAM plus RAMIN. The guest memory owner
initializes this view and implements `K_MmClaimGpuInstanceMemory` as a
page-aligned, bounded bump allocator over the RAMIN portion. The allocator
returns a guest-process pointer into the shared mapping and reports alignment
padding.

The guest memory module now exposes
`XboxKernelResolveGpuPhysicalAddress(physicalAddress, bytes, access)` for
bounded ranges in the 64 MiB shared unified-memory window. It rejects zero
lengths, invalid access masks, and ranges that overflow the backing store; the
returned pointer is into the guest's shared view, so host and guest observe the
same bytes. The host-side memory test covers read/write translation, shared
view coherence, and end-of-window rejection.

This is a **first bounded address translation step, not a complete
physical-memory/DMA subsystem**. The access mask currently validates the
requested operation but does not enforce per-page permissions; there is no
allocation metadata for arbitrary physical pages, fence-aware free/reuse,
device interrupt delivery, or end-to-end guest DMA command test. The current
instance allocator remains monotonic for the lifetime of the guest and is
reset only when the shared mapping is initialized again.

### Required contract

NV2A must not treat a host pointer as a guest physical address. The kernel
memory owner must provide the single allocation/translation authority. The
planned shared interface is:

```cpp
// Proposed kernel-owned GPU arena API; not implemented yet.
NTSTATUS XboxKernelGpuMemoryInitialize(SIZE_T arenaBytes);
void XboxKernelGpuMemoryShutdown();

NTSTATUS XboxKernelGpuAllocateInstanceMemory(
    SIZE_T bytes, SIZE_T alignment, ULONG flags,
    void** guestVirtualAddress, ULONG* guestPhysicalAddress);
NTSTATUS XboxKernelGpuFreeInstanceMemory(void* guestVirtualAddress);

NTSTATUS XboxKernelResolveGuestPhysicalAddress(
    ULONG guestPhysicalAddress, SIZE_T bytes, ULONG access,
    void** guestVirtualAddress);
```

Ownership rules:

- The kernel memory subsystem owns allocation metadata, physical-address
  assignment, bounds checking, access permissions, and release.
- NV2A may retain guest physical addresses and offsets, never host pointers
  into a guest process.
- The host maps the same backing arena into its own address space and resolves
  a guest physical range to an arena offset. It must validate the complete
  range (including integer overflow) before reading or writing.
- Free is rejected while a GPU command/fence still references the allocation.
  Teardown stops the GPU worker and drains outstanding work before releasing
  the arena.
- Read/write permissions are checked by the kernel resolver. GPU DMA reads
  require guest-read permission; DMA writes require guest-write permission.
- The arena size and physical layout must be selected by the kernel memory
  owner based on the actual Xbox memory map. Do not hard-code an arbitrary
  64 MiB arena or invent physical addresses in the NV2A implementation.

The NV2A developer must use this API rather than adding a second allocator.

## 3. MMIO

`K_MmMapIoSpace` and `K_MmUnmapIoSpace` now recognize the NV2A aperture
(`0xFD000000`–`0xFE000000`) and return/accept the guest-visible device
address without mapping host physical memory. The guest installs a vectored
exception handler. Its x86 decoder handles a limited set of absolute and
register-indirect byte/word/dword loads and stores, including immediate stores
and sign/zero-extending byte/word loads, then forwards them through the GPU
RPC client. Unsupported instructions or failed RPCs continue exception
search rather than being silently emulated.

This is a first MMIO path, not a complete mapping registry: current map/unmap
validation is range-based, not allocation-backed, and the decoder supports
only its explicitly implemented instruction forms. Expand tests before
claiming arbitrary guest MMIO instructions are supported.

## 4. PCI and DMA

The existing declaration is:

```cpp
void __stdcall K_HalReadWritePCISpace(
    ULONG bus, ULONG slot, ULONG offset, void* buffer,
    ULONG length, BOOLEAN write);
```

Its current implementation raises the unsupported-hardware-import exception.
There is no PCI configuration-space provider or DMA resolver on this branch.

The NV2A service must expose a PCI config-space operation through the same GPU
provider/transport. `HalReadWritePCISpace` validates the guest buffer and
requested config-space range, then forwards the bounded read/write to that
provider. Do not call host `\Device\PhysicalMemory`, enumerate a real host
PCI device, or assume the host has an NV2A device.

DMA uses `XboxKernelResolveGuestPhysicalAddress` above. There must be one
memory owner and one physical-address map shared by NV2A and other devices.

## 5. Interrupts

The combined kernel already has virtual interrupt records in
`guest32/kernel_thread_exports.cpp`:

```cpp
void __stdcall K_KeInitializeInterrupt(
    void* interrupt, ServiceRoutine serviceRoutine, void* serviceContext,
    ULONG vector, UCHAR irql, ULONG interruptMode, BOOLEAN shareVector);
BOOLEAN __stdcall K_KeConnectInterrupt(void* interrupt);
BOOLEAN __stdcall K_KeDisconnectInterrupt(void* interrupt);
```

These APIs register, connect, and disconnect a virtual interrupt object.
They do **not** receive physical/device interrupts yet. The implementation
explicitly documents that physical delivery is unavailable until a
bus/interrupt-controller backend exists.

Required GPU lifecycle:

- Initialize records the ISR/context/vector/IRQL using the existing interrupt
  manager. NV2A must not create a parallel interrupt registry.
- Connect registers the guest interrupt with the existing manager and enables
  the provider's GPU interrupt source.
- Disconnect disables the provider source, drains queued notifications, and
  disconnects the existing record.
- GPU worker notifications must be queued to the kernel interrupt manager;
  the ISR runs on the guest side under the kernel's interrupt/IRQL rules.
- No host worker may call a guest function pointer directly.

The provider-to-kernel interrupt notification callback and queueing bridge
still need implementation.

## 6. Display

`AvSetDisplayMode` is currently routed to the unsupported export path.
There is no combined-kernel framebuffer, swapchain, or presentation contract.

The planned guest-facing operation is:

```cpp
NTSTATUS XboxKernelGpuSetDisplayMode(
    ULONG width, ULONG height, ULONG refreshRate,
    ULONG format, ULONG flags);
```

It forwards a validated display-mode message over the GPU service channel.
The host NV2A/presentation provider owns the swapchain/window and reports
failure explicitly. Presentation and GPU execution must use the same provider
lifetime; mode changes are serialized with GPU flush/flip operations.

Do not expose a host D3D object or window handle to guest code.

## 7. Existing tests and required new tests

Existing combined-branch coverage:

- `tests/kernel_guest_backend_smoke.cpp`: shared XBE mapping, x86 guest entry,
  TLS template/zero-fill, attach/detach callbacks, stop/release.
- `tests/kernel_memory_guest_smoke.cpp` and
  `tests/kernel_memory_host_tests.cpp`: guest memory ABI and 32-bit range
  arithmetic.
- `tests/kernel_io_group_a_guest_smoke.cpp`: kernel export dispatch smoke
  coverage, including current memory/interrupt export behavior.

The combined branch has `kernel_gpu_service_guest_smoke` for the guest
RPC client and `kernel_gpu_mmio_guest_smoke` for representative x86 MMIO
loads/stores crossing the RPC pipe. CI builds the NV2A provider separately,
stages `nv2a.dll` beside `xbox_kernel.dll`, and builds/runs the guest GPU
smoke targets. These tests exercise the client and decoder contracts; they do
not yet prove that a real guest XBE rendered a frame.

**Pixel readback path already present:** after `XboxNV2A_Flush()` flushes the
PGRAPH translator, it calls `sync_surface_to_vram()`. That function locks the
bound color render target read-only and copies each row into the NV2A VRAM
surface span. The existing `XboxNV2A_ReadVRAM` provider export and
`XboxKernelGpuReadVram` guest RPC client can then retrieve those pixels.
This is a real render-target-to-VRAM readback path, not merely a frame/draw
counter. It is conditional on a supported NV097 surface being bound and
successfully synchronized. The current GPU RPC transfers are capped at 64 KiB
per request, so a full 640x480x4 framebuffer must be read in bounded chunks.
The existing smoke test verifies arbitrary VRAM RPC bytes, but not pixels
produced by a real draw; the latter remains an end-to-end test gap.

**Physical DMA boundary:** `XboxKernelResolveGpuPhysicalAddress` currently
validates and translates addresses only within the shared 64 MiB GPU VRAM
window. It is not a general guest system-RAM mapper.

The NV2A provider now exposes `XboxNV2A_BindGuestPhysicalMemory(base, view,
length)` and routes validated PCI/AGP DMA spans through that explicitly
bound window. The span resolver is used by DMA pushbuffer reads, vertex-array
reads, and linear/mip/paletted texture decoding; it checks both the DMA
object's limit and the bound window before producing a host pointer. Unbinding
or changing the mapping invalidates cached DMA contexts. Local VRAM DMA keeps
using the existing VRAM path.

**Important:** the current kernel backend does not yet own a true guest
physical-RAM section. Its `hostView` is the XBE's guest virtual image mapping,
not a physical-memory aperture, so the kernel deliberately does not bind it
as PCI/AGP memory. PCI/AGP DMA therefore remains unavailable at runtime until
a real guest physical RAM mapping and guest virtual-to-physical ownership
contract are implemented. The provider-side bounded DMA support is ready for
that mapping, but this is not end-to-end system-memory DMA.

Required follow-up tests:

1. GPU pipe startup ping from `xbe_guest32` to the host worker, with request
   ID/version validation.
2. Register read/write widths 1, 2, and 4; invalid widths and unknown opcodes.
3. Exact 64 KiB transfer accepted; 64 KiB + 1 rejected before allocation.
4. Disconnect during request; worker exits and is joined before pipe close.
5. GPU arena allocation, physical translation, access denial, overflow, and
   free-after-fence behavior.
6. MMIO access decoder tests for supported x86 load/store forms and
   instruction-state preservation.
7. PCI bounds and read/write tests.
8. Interrupt connect/disconnect, notification ordering, ISR delivery, and
   teardown with queued interrupts.
9. Display mode change/flip and provider shutdown tests.
10. End-to-end guest XBE writes NV2A registers, submits a pushbuffer, flushes,
    reads back the bound surface's VRAM span through the guest RPC, and checks
    known pixel values.

## 8. Small guest-to-host example

The wire-level ping is deliberately provider-independent and should be the
first end-to-end test. After the dedicated GPU pipe is opened, the guest sends:

```cpp
XboxGpuRpcRequest request = {
    XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION,
    XboxGpuRpcPing, 0, nextRequestId++,
    0, 0, 0, 0
};
XboxGpuRpcResponse response{};
WriteExact(gpuPipe, &request, sizeof(request));
ReadExact(gpuPipe, &response, sizeof(response));
if (response.magic != XBOX_GPU_RPC_MAGIC ||
    response.version != XBOX_GPU_RPC_VERSION ||
    response.request_id != request.request_id ||
    response.status != ERROR_SUCCESS) {
    // Treat the GPU service as disconnected/unavailable.
}
```

The host worker reads the fixed-size request, validates it, and answers the
same request ID with `value = XBOX_GPU_RPC_VERSION`. This proves that the
guest request crossed the process boundary and reached the host service
without depending on an NV2A rendering implementation.

**Current implementation status on this branch:** the combined branch has
the dedicated GPU pipe, host worker, guest client, shared VRAM/RAMIN mapping,
bounded RAMIN instance allocator, bounded GPU-VRAM physical-address resolver,
NV2A provider build/staging in CI, and a limited MMIO fault-emulation path
with guest smoke tests. The provider is loaded by `xbox_kernel.dll` from the
same output directory and attached to the shared memory view. Surface-to-VRAM
pixel readback exists as described above, but an end-to-end guest draw/readback
assertion has not yet been run. Remaining major integration work includes
general system-memory DMA translation, PCI config-space forwarding,
provider-to-kernel interrupt notification through the existing interrupt
manager, display-mode and present/flip forwarding, and the end-to-end XBE
render test.
