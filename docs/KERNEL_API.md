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


## Guest32 XNet/XSocket integration

The x86 guest target now compiles the networking implementation from
`network_compat.cpp` and `xnet_compat.cpp`. Title-facing declarations retain
the manifest's `WINAPI`/stdcall ABI; the internal `XboxNet*` bridge remains
private and `__cdecl`. These are guest-process functions, not kernel export
ordinals.

`guest_api_registry.cpp` provides a name-based lookup for the 29 title-facing
XNet/XSocket functions. `XboxGuestApiLookup("XSocketConnect")` returns a
32-bit address that is valid only inside the x86 guest process. The registry
does not export host pointers, does not add fake kernel ordinals, and rejects
unknown names. The x86 smoke test resolves every registered name and invokes
the byte-order functions through their returned addresses.

**Important limitation:** this registry is a guest-side callable-function
inventory/lookup surface. It does not by itself redirect statically linked
XNet call sites in arbitrary retail XBEs, and the current kernel thunk resolver
does not treat XNet as kernel imports. Static-call interception still requires
a verified title-specific boundary and tests. Do not advertise arbitrary-title
XNet reachability yet.

Build and test the guest layer on Windows with:

```powershell
cmake -S guest32 -B build-guest32 -A Win32
cmake --build build-guest32 --config Release --target xbe_guest32 network_smoke guest_api_registry_smoke
ctest --test-dir build-guest32 -C Release --output-on-failure -R "network_smoke|guest_api_registry_smoke"
```

The kernel export audit remains a separate check: a mapped ordinal is not
proof of correct API semantics. The guest32 CI job runs both the export audit
and the networking/registry smoke tests.


## ABI v2 XBE execution backend

The host-side `xbox_kernel.dll` implements the coordinator-approved
`XBE_EXECUTION_BACKEND_V2` contract from
`modules/xbe_loader/xbe_backend_api.h`. ABI v1 remains unchanged and is
explicitly refused by this provider because its startup callback lacks the
validated image description and mapped view.

The host reserves the exact requested guest base using a pagefile-backed
section, maps that section at the same numeric base in its own process, and
starts `xbe_guest32.exe`. The x86 process opens the named section and maps
the same backing pages at the exact XBE base. Image mapping is performed by
the loader into the host view; the guest resolves kernel imports and applies
section protections in its own address space before starting the guest thread.
The entry point remains a 32-bit guest VA and is never called by the x64 host.

The host/guest control channel is a fixed-width named-pipe protocol in
`guest32/guest_protocol.h`. Startup carries the complete validated image
metadata needed by the guest: base, image size, header size, entry point,
TLS address, and section count. The guest signals readiness only after TLS
allocation and attach callbacks complete. On unload, the host requests stop;
if title code is still running, the isolated guest process is terminated before
the shared image view is released. This prevents freeing code/TLS while the
guest thread is executing.

TLS setup allocates a per-thread block, copies the image template, adds zero
fill, writes the allocated TLS index, runs attach callbacks before the entry
point, runs detach callbacks in reverse order after the entry returns, and
then frees the block/index. The guest process provides the x86 thread/TEB
environment; this is not a claim of full Xbox scheduler or IRQL equivalence.

## Guest networking lookup

The guest API registry contains 29 title-facing XNet/XSocket functions. The
host export `XboxKernelGetGuestApiAddress(name)` asks the running x86 guest
process for the corresponding 32-bit guest address. Returned addresses are
valid only in that guest process. This is a name-based lookup, not a kernel
ordinal table, and unknown names return zero.

The lookup makes verified call-site redirection possible without passing
host pointers into the guest. It does **not** patch statically linked XNet
routines automatically. The loader still needs a per-title verified
interception strategy and call-site tests before arbitrary XBE networking can
be claimed.

Current build/test automation compiles the x64 kernel provider, main runtime
module, x86 guest process, and both network/registry smoke tests. A successful
compile does not prove a real XBE starts or connects to Insignia.
