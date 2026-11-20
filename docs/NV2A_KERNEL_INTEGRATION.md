# NV2A integration contract for the combined kernel

Target branch: `work/kernel-combined-clean-import`

This document separates **interfaces that already exist** from the GPU-facing
interfaces that still need implementation. It is the handoff contract for the
NV2A developer. It does not claim that GPU memory, MMIO, PCI, interrupts, or
display integration are already working.

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

The current implementations in `guest32/kernel_memory.cpp` are explicit
stubs: GPU instance-memory allocation returns null, and I/O-space map/unmap
return null/no-op. The guest process has normal guest-VA virtual-memory
operations and the shared XBE image mapping, but **there is no general shared
GPU/DMA arena, physical-address resolver, or memory ownership API yet**.

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

`MmMapIoSpace` and `MmUnmapIoSpace` are currently not implemented. Windows
physical mappings must not be attempted for Xbox chipset addresses.

The agreed implementation model is a **guest MMIO aperture**, not a native
host physical mapping:

- The kernel validates the requested Xbox physical range and reserves a
  guest-VA aperture for the mapping.
- GPU-register ranges are tagged as device mappings and associated with the
  NV2A provider; ordinary RAM mappings continue through the kernel memory
  manager.
- Guest loads/stores to the aperture are trapped by a guest-side access
  handler and translated to `XboxKernelGpuReadRegister` /
  `XboxKernelGpuWriteRegister` RPCs. The handler must preserve the guest
  instruction's width and sign/zero-extension semantics.
- Unmap invalidates the aperture and waits for in-flight accesses before the
  mapping record is freed.

The trap/decoder and the mapping registry do not exist on the combined branch
yet. Do not return a normal writable RAM pointer for the NV2A register range:
that would make writes invisible to the service.

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

There are no NV2A tests in the combined branch yet. PR #37 adds
`tests/nv2a_rpc_smoke.cpp` on its older branch; it covers register/method/
flush and bounded VRAM/RAMIN transfers over a loopback pipe, but it does not
exercise the combined backend or the hardware-facing kernel exports.

Required combined-branch tests:

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
10. End-to-end guest XBE writes NV2A registers, submits a pushbuffer, and
    reaches a host provider; verify a rendered frame rather than merely a
    successful XBE load.

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

**Current implementation status:** the combined branch has the execution
backend/control pipe and the shared GPU RPC wire header. The dedicated GPU
pipe, host worker, guest client, GPU memory arena, MMIO trap, PCI forwarding,
interrupt notification, and display forwarding are still implementation
tasks. PR #37 supplies a useful older-branch reference for the first four RPC
operations and bounded memory transfers, not a completed combined-kernel
integration.
