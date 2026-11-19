# Xbox kernel object-body reference exports

Integrated branch: `work/kernel-combined-clean-import`
NXDK ABI reference: `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`

## Implemented slice

- Ordinal 248 `ObReferenceObjectByPointer` — NTAPI; increments the reference count for a manager-owned object-body pointer when `ObjectType == NULL`. Non-null type descriptors are rejected with `STATUS_OBJECT_TYPE_MISMATCH`; null or unowned pointers return `STATUS_INVALID_PARAMETER`. `DesiredAccess` and `AccessMode` are currently ignored because this manager has no security descriptor or OBJECT_TYPE model.
- Ordinal 246 `ObReferenceObjectByHandle` — NTAPI; for a valid shared-manager handle and `ObjectType == NULL`, acquires and returns the object-body pointer. Non-null type pointers return `STATUS_OBJECT_TYPE_MISMATCH` until Xbox OBJECT_TYPE descriptors are implemented; invalid handles return `STATUS_INVALID_HANDLE`.
- Ordinal 251 `ObfReferenceObject` — FASTCALL; increments a reference for payload pointers currently owned by the shared guest object manager.
- Ordinal 250 `ObfDereferenceObject` — FASTCALL; drops a reference for shared-manager payload pointers and invokes the registered destructor exactly once when the last reference is released.
- For compatibility with the existing Win32-backed event path, dereferencing a valid native Win32 HANDLE is a no-op, matching the Cxbx-Reloaded compatibility behavior.

The shared object manager rejects new references while title shutdown is in progress, but permits outstanding references to be released. Destructors execute outside the manager lock.

## Limits

These routines currently support only object-body pointers registered with `XboxKernelCreateObjectHandle`. Other kernel object layouts are not yet represented by the shared manager; an unknown object pointer is fail-fast unsupported rather than having its memory interpreted as an invented OBJECT_HEADER. `ObfReferenceObject` on native Win32 handles is not supported.

## Tests

The x86 smoke test now invokes ordinals 246, 248, 250, and 251 through `KernelDispatch` using the real stdcall/fastcall stack/register conventions. It verifies handle lookup, pointer references, fastcall increment/decrement, invalid-handle status/output clearing, and exactly-once destruction after the final handle close. The focused object-reference smoke test also covers null and unknown-type rejection and native Win32 HANDLE dereference compatibility.
