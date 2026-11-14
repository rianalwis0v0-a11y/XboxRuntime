# Xbox guest object handle manager

Branch: `work/kernel-memory-apis`

## Purpose

The guest object manager provides one shared 32-bit guest-handle namespace for kernel objects. Guest tokens are opaque handle values and are not native Win32 `HANDLE` values. The implementation lives in `guest32/kernel_object_manager.cpp`; declarations are in `guest32/kernel_object_manager.h`.

## Lifecycle contract

- `XboxKernelCreateObjectHandle` publishes one handle-table entry and takes one manager reference. If it fails, payload ownership remains with the caller.
- `XboxKernelAcquireObjectReference` validates the handle and requested type, then takes one temporary reference. Every successful acquire must be paired with exactly one `XboxKernelReleaseObjectReference`.
- On acquire failure, a valid output pointer is set to `nullptr`. `XboxKernelGetLastObjectLookupStatus` distinguishes invalid handle, type mismatch, invalid parameter and shutdown.
- `XboxKernelDuplicateObjectHandle` creates a new token for the same payload/object identity. Shared mutable state and IOCP packet queues therefore remain shared.
- `XboxKernelCloseObjectHandle` removes one token and releases exactly one reference.
- The payload destroy callback runs once, outside the manager lock, after the last handle and acquired reference is released.
- `XboxKernelCloseAllObjectHandles` blocks new creates/acquires and drains all published handles. Acquired references remain valid until released.
- `XboxKernelBeginObjectHandleSession` reopens registration for a new title session after shutdown.

## Object types

`Event`, `Semaphore`, `Mutant`, `Timer`, `IoCompletion`, `SymbolicLink`, and `Directory` are reserved in the shared enum. The manager owns generic handle/refcount behavior; each subsystem owns its payload and destroy callback.

## NTSTATUS behavior

| Condition | Status / behavior |
| --- | --- |
| Invalid handle | `STATUS_INVALID_HANDLE` |
| Expected type differs | `STATUS_OBJECT_TYPE_MISMATCH` via lookup status |
| Invalid arguments/options | `STATUS_INVALID_PARAMETER` |
| Handle table is shutting down | `STATUS_INVALID_DEVICE_STATE` (`0xC0000184`) |
| Token/object allocation failure | `STATUS_INSUFFICIENT_RESOURCES` (`0xC000009A`) |
| Unsupported cross-process duplication | `STATUS_NOT_SUPPORTED` (not implemented) |

`K_NtClose` and `K_NtDuplicateObject` route manager-owned tokens through this table. Unrelated native Win32 handles retain the existing native fallback path. Same-process duplicate handles share the same payload; `DUPLICATE_CLOSE_SOURCE` closes only the source entry after publishing the duplicate.

## Verification

`kernel_handle_guest_smoke` covers x86 guest token creation, type mismatch/output clearing, shared duplicate identity/state, close/reference accounting, exactly-once destruction, shutdown draining, and the pre-existing native-handle duplicate path. The dedicated workflow run `43586237090` passed both x86 guest and x64 host jobs.

## Explicit limits

- Cross-process handle duplication is not supported.
- Named object namespace lookup and directory/symbolic-link APIs are separate work; enum values do not mean those APIs are implemented.
- A successful `Acquire` must always be balanced by one `Release`; callers must not retain raw payload pointers after releasing their reference.
- This is the shared generic handle table, not a full NT Object Manager implementation.