# Xbox kernel API surface

## How XBE kernel imports are exposed

Retail Xbox XBE files do not import a normal Windows `xboxkrnl.dll`. They carry an
encoded kernel thunk-table address and ordinal entries. XboxRuntime decodes the
table in the 32-bit guest process, replaces each ordinal with a guest-callable
x86 thunk, and routes the thunk through `KernelDispatch` to a `K_*` implementation.
This preserves the guest x86 calling convention and avoids pretending that a
64-bit host DLL can be called directly by the 32-bit guest.

The public ordinal catalogue is `tools/kernel_exports.json`. The dispatcher and
data-export mappings live in `guest32/kernel_compat.cpp`.

## Coverage accounting

The catalogue currently contains 371 export entries. The dispatch audit counts
an ordinal as mapped when it has a function route or a data-export address.
That is only dispatch coverage: it does not prove that the API has correct Xbox
semantics, structure layouts, timing, IRQL behaviour, or side effects.

The dispatcher deliberately sends unknown/unimplemented ordinals to
`K_Unsupported`, which raises a guest exception. This is a fail-fast path, not
a successful stub implementation. Do not count it as API support.

Run:

```powershell
python tools/audit_kernel_exports.py
python tools/audit_kernel_exports.py --check
```

The diagnostic XBE in `.github/scripts/test-diagnostic-xbe.ps1` calls selected
exports and checks observable results. A passing audit alone is not a kernel
compatibility test.

## Recently added RTL arithmetic

- Ordinal 281, `RtlExtendedIntegerMultiply`
- Ordinal 282, `RtlExtendedLargeIntegerDivide`
- Ordinal 283, `RtlExtendedMagicDivide`

These have guest x86 dispatch routes and are exercised by the diagnostic XBE.
Divide-by-zero and invalid shift inputs raise guest status exceptions.

## Still incomplete

Many catalogued APIs remain intentionally unsupported, including substantial
parts of the I/O manager, object manager, device/interrupt queues, physical
hardware access, crypto, and kernel debugging/memory diagnostics. These need
real implementations and behavioural tests before they can be advertised as
supported. A mapped ordinal is not equivalent to a working game-facing API.
