# Xbox kernel object-body reference exports

Branch: `work/kernel-crypto-apis`
PR: https://github.com/rianalwis0v0-a11y/XboxRuntime/pull/24
NXDK ABI reference: `fafbe41b48fa00a31b79dd49ad1f86a30adcdd9e`

## Implemented slice

- Ordinal 251 `ObfReferenceObject` — FASTCALL; increments a reference for payload pointers currently owned by the shared guest object manager.
- Ordinal 250 `ObfDereferenceObject` — FASTCALL; drops a reference for shared-manager payload pointers and invokes the registered destructor exactly once when the last reference is released.
- For compatibility with the existing Win32-backed event path, dereferencing a valid native Win32 HANDLE is a no-op, matching the Cxbx-Reloaded compatibility behavior.

The shared object manager rejects new references while title shutdown is in progress, but permits outstanding references to be released. Destructors execute outside the manager lock.

## Limits

These routines currently support only object-body pointers registered with `XboxKernelCreateObjectHandle`. Other kernel object layouts are not yet represented by the shared manager; an unknown object pointer is fail-fast unsupported rather than having its memory interpreted as an invented OBJECT_HEADER. `ObfReferenceObject` on native Win32 handles is not supported.

## Tests

The x86 FASTCALL smoke test verifies reference survival after the last handle closes, exactly-once destruction on the final dereference, null rejection, and the native Win32 HANDLE dereference no-op.
