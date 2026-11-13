# Xbox kernel API surface

## How XBE kernel imports are exposed

Retail Xbox XBE files do not import a normal Windows `xboxkrnl.dll`. They carry an encoded kernel thunk-table address and ordinal entries. XboxRuntime decodes the table in the 32-bit guest process, replaces each ordinal with a guest-callable x86 thunk, and routes the thunk through `KernelDispatch` to a `K_*` implementation. This preserves the guest x86 calling convention and avoids calling a 64-bit host DLL directly from the 32-bit guest.

The public ordinal catalogue is `tools/kernel_exports.json`. The dispatcher and data-export mappings live in `guest32/kernel_compat.cpp`.

## Coverage accounting

The catalogue contains 371 export entries. The dispatch audit counts an ordinal as mapped when it has a function route or a data-export address. This is dispatch coverage only; it does not prove correct Xbox semantics, structure layouts, timing, IRQL behaviour, or side effects.

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

## Still incomplete

Many catalogued APIs remain unsupported, including substantial parts of the I/O manager, object manager, device/interrupt queues, physical hardware access, crypto, and kernel debugging/memory diagnostics. These need real implementations and behavioural tests before they can be advertised as supported. A mapped ordinal is not equivalent to a working game-facing API.
