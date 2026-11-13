# Xbox kernel API surface

## How XBE kernel imports are exposed

Retail Xbox XBE files do not import a normal Windows `xboxkrnl.dll`. They carry an encoded kernel thunk-table address and ordinal entries. XboxRuntime decodes the table in the 32-bit guest process, replaces each ordinal with a guest-callable x86 thunk, and routes the thunk through `KernelDispatch` to a `K_*` implementation. This preserves the guest x86 calling convention and avoids calling a 64-bit host DLL directly from the 32-bit guest.

The public ordinal catalogue is `tools/kernel_exports.json`. The dispatcher and data-export mappings live in `guest32/kernel_compat.cpp`.

## Coverage accounting

The catalogue contains 371 export entries. The dispatch audit counts an ordinal as mapped when it has a function route or a data-export address. This is dispatch coverage only; it does not prove correct Xbox semantics, structure layouts, timing, IRQL behaviour, or side effects. The current integration snapshot has 371 exports: 208 function routes plus 34 data exports (242 mapped, 129 unmapped).

Unknown/unimplemented ordinals go to `K_Unsupported`, which raises a guest exception. This is fail-fast behaviour, not a successful stub. Do not count it as API support.

Run:

```powershell
python tools/audit_kernel_exports.py
python tools/audit_kernel_exports.py --check
```

The diagnostic XBE in `.github/scripts/test-diagnostic-xbe.ps1` calls selected exports and checks observable results. A passing audit alone is not a kernel compatibility test.

## RTL arithmetic exports added

- Ordinal 281: `RtlExtendedIntegerMultiply`
- Ordinal 282: `RtlExtendedLargeIntegerDivide`
- Ordinal 283: `RtlExtendedMagicDivide`

These have guest x86 dispatch routes and diagnostic-XBE coverage. Divide-by-zero and invalid shift inputs raise guest status exceptions.

## 64-bit interlocked arithmetic exports

- Ordinal 19: `ExInterlockedAddLargeInteger` acquires the guest-provided spin lock, adds a signed 64-bit increment, releases the lock, and returns the previous value.
- Ordinal 20: `ExInterlockedAddLargeStatistic` uses a 64-bit compare/exchange loop to atomically add the unsigned 32-bit increment.
- Ordinal 21: `ExInterlockedCompareExchange64` performs an atomic 64-bit compare/exchange and returns the value observed before the operation.
- The diagnostic XBE exercises ordinals 20 and 21, including the expected resulting 64-bit memory values. Ordinal 19 is routed but its by-value 64-bit return and guest spin-lock ABI still need a dedicated guest-side test before it is counted as behaviourally verified.

## Interlocked list exports added

- Ordinal 32: `ExfInterlockedInsertHeadList`
- Ordinal 33: `ExfInterlockedInsertTailList`
- Ordinal 34: `ExfInterlockedRemoveHeadList`

These operate on the guest's circular doubly-linked `LIST_ENTRY` layout and acquire/release the caller-provided 32-bit spin lock. The diagnostic XBE checks empty-list insertion, head/tail ordering, removal order, and the empty result. They are not a substitute for the separate executive I/O/object manager work.

## Additional kernel implementations

- Ordinals 106, 114, 115, 133, 134, and 135: device-queue initialization, FIFO/keyed insertion, FIFO/keyed removal, and entry removal. The guest x86 queue and entry layouts are represented explicitly; operations are serialized across host threads.
- Ordinal 195: `NtDeleteFile`, using the same Xbox device-path translation and NTSTATUS mapping as the file-open path.
- Ordinal 234: `NtWaitForSingleObjectEx`, routed to the alertable single-object wait implementation.
- NT wait APIs now share conversion for negative relative and positive absolute 100-ns deadlines. Alertable waits report `STATUS_USER_APC`; timeout reports `STATUS_TIMEOUT`.

These implementations are newly routed, but still need guest-side behavioural coverage and comparison against retail Xbox timing/queue edge cases.

## Kernel work queues

- Ordinals 111, 116, 117, and 136: `KeInitializeQueue`, `KeInsertHeadQueue`, `KeInsertQueue`, and `KeRemoveQueue` now use the guest's native x86 KQUEUE/LIST_ENTRY layout with synchronized host-side wait state.
- Queue signal count, current worker count, maximum concurrency, head/tail insertion, FIFO removal, and relative/absolute timeout conversion are maintained. A timeout returns `STATUS_TIMEOUT` in the pointer-sized return value, matching the NT wait convention.
- The diagnostic XBE verifies head/tail ordering and the empty-queue timeout path.

## Directory enumeration

- Ordinal 207, `NtQueryDirectoryFile`, supports synchronous single-entry `FileDirectoryInformation` queries against opened host-backed directory handles.
- Xbox's directory record uses a 64-byte header and an ANSI filename. The adapter translates the handle path, applies an optional ANSI wildcard mask, skips `.` and `..`, preserves enumeration state between calls, and releases that state when `NtClose` closes the directory handle.
- Event signaling and `IO_STATUS_BLOCK` completion are synchronous. Guest APC callbacks and other information classes are explicitly unsupported.

## I/O invalid-device dispatch

- Ordinal 74, `IoInvalidDeviceRequest`, returns `STATUS_INVALID_DEVICE_REQUEST` (0xC0000010), the documented fallback status for an unsupported device request.
- The diagnostic XBE verifies the exact NTSTATUS.

## CPU context capture

- Ordinal 265, `RtlCaptureContext`, captures the current native x86 guest-thread context into the caller-provided x86 `CONTEXT` structure.
- The diagnostic XBE checks that captured EIP and ESP are populated. This captures the actual host thread context at the kernel-call boundary; it does not reconstruct an earlier Xbox exception frame.

## Timer arming

- Ordinal 229, `NtSetTimerEx`, forwards timer scheduling to the host NT `NtSetTimer` routine after validating the handle, due time, and period.
- The Xbox APC mode field is accepted but not used when no APC is requested. Guest APC routine/context pointers are rejected with `STATUS_NOT_SUPPORTED`; passing guest addresses as host callback pointers would be unsafe.
- The previous timer state is returned by the native routine. The diagnostic XBE arms a created timer with a short relative due time and verifies the call and previous-state output.

## File attribute query

- Ordinal 210, `NtQueryFullAttributesFile`, translates absolute Xbox device paths through the guest title path mapping and queries the host file metadata.
- Returns Xbox `FILE_NETWORK_OPEN_INFORMATION` layout (six 64-bit time/size fields followed by attributes), with Win32 creation/access/write times and logical file size.
- The Win32 query does not expose a distinct change timestamp or allocation size through this API, so change time mirrors last-write time and allocation size mirrors logical EOF. Relative names using a root-directory handle are explicitly unsupported rather than misresolved.
- Diagnostic XBE verifies the file length and nonzero file attributes against its known 8-byte fixture.

## NT dispatcher-object query APIs

- Ordinals 209, 213, 214, and 216: `NtQueryEvent`, `NtQueryMutant`, `NtQuerySemaphore`, and `NtQueryTimer` forward the Xbox basic-information query ABI to the corresponding Windows NT native query routines. Native NTSTATUS values and required-buffer/return-length behavior are preserved.
- The guest diagnostic XBE creates and queries a signaled event, a 2/5 semaphore, an unowned mutant, and an unsignaled timer, then closes each object.
- `NtCreateSemaphore` now accepts the full five-argument Xbox ABI (output handle, access mask, object attributes, initial count, maximum count). The previous wrapper omitted the access-mask argument and shifted every later argument.

## Filesystem volume information

- Ordinal 218: `NtQueryVolumeInformationFile` supports `FileFsVolumeInformation`, `FileFsSizeInformation`, `FileFsDeviceInformation`, and `FileFsAttributeInformation` for host-backed files. It obtains volume labels, serial numbers, allocation-unit counts, sector geometry, device type, filesystem flags, and filesystem names from the Windows volume APIs.
- The diagnostic XBE checks all four information classes against its mounted test volume, including sector geometry, disk device type, and non-empty filesystem metadata.
- Windows does not expose the original Xbox volume creation timestamp through these APIs; the returned creation-time field is zero. This is a documented compatibility limitation, not full Xbox volume emulation.

## Still incomplete

Many catalogued APIs remain unsupported, including substantial parts of the I/O manager, object manager, device/interrupt queues, physical hardware access, crypto, and kernel debugging/memory diagnostics. These need real implementations and behavioural tests before they can be advertised as supported. A mapped ordinal is not equivalent to a working game-facing API.
