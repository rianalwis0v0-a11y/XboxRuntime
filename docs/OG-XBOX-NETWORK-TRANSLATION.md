# Original Xbox Networking — Windows Translation Backend

> This replaces the earlier MCPX hardware-emulation direction. XboxRuntime's goal here is **API/operation translation**, not an emulated Ethernet controller.

## Goal

Run Original Xbox networking behaviour in the 32-bit guest while using Windows networking facilities on the host. Do not model MCPX PCI registers, PHY, DMA descriptor rings, or Ethernet hardware.

## Translation architecture

```
Original Xbox title / XNet code
          |
          v
Xbox ABI compatibility boundary (x86 guest)
          |
          v
XboxRuntime XNet / XSocket / WSA compatibility functions
          |
          v
Guest-safe handles + structure conversion
          |
          v
Windows Winsock 2 + IP Helper + Windows DNS
          |
          v
Windows network adapters / router / Internet
```

- **Winsock 2**: TCP/UDP sockets, bind/connect/listen/accept, send/receive, select, options, shutdown and close.
- **Windows DNS / resolver**: resolve hostnames; Insignia DNS may be selected as the configured resolver, not hardwired into individual API calls.
- **IP Helper (GetAdaptersAddresses)**: report host link state, IPv4 configuration, gateway, DNS and MAC information through Xbox-shaped XNADDR/link-status results.
- **ABI boundary**: retain 32-bit guest handles and pointers; validate guest buffers and translate structures/flags/errors rather than leaking host pointers or 64-bit SOCKET values.
- **Online services**: Xbox Live/Insignia service implementation is out of scope; this layer only transports title networking traffic to the network.

## Existing implementation discovered

The integration branch already contains:

- guest32/network_compat.cpp/.h: Winsock 2.2 lifetime, IPv4 resolution, TCP/UDP socket transport and 32-bit handle tokens.
- guest32/xnet_compat.cpp/.h: initial XNet startup/cleanup, DNS, XNADDR/link status, XSocket wrappers and WSA helper wrappers.
- tests/network_smoke.cpp: intended local transport/API smoke coverage.
- docs/INSIGNIA-ACCEPTANCE.md: milestones and explicit limitations.

These are a useful foundation. Do not rewrite them as an MCPX driver.

## Remaining blocker: statically linked XNet call sites

XNet is commonly statically linked into retail titles. Merely exporting functions named XNetStartup or XSocketSend from the guest executable does not automatically redirect a title's already-linked internal calls to those functions.

The x86 guest now resolves named imports from the XBE non-kernel import directory when the library is XNET / XNET.DLL, binding the implemented XNet/XSocket names to guest function addresses. This covers import-table calls only. Retail titles may also contain statically linked XNet code whose internal calls never appear in that directory; those call sites are still not redirected. Do not guess import ordinals, patch arbitrary byte patterns, or claim compatibility until a specific title's call path is identified.

Preferred implementation order:

1. Audit the x86 guest's kernel import resolver and backend interface. Identify which Xbox networking-related kernel exports are actually imported by the title and what ABI they use.
2. Verify how statically linked XNet reaches lower-level kernel services in a real XBE. Use NXDK/public symbol references and controlled XBE fixtures; distinguish documented facts from hypotheses.
3. Add a narrow, opt-in translation hook at a verified boundary. Preserve original calling conventions, guest memory validation, return codes, last-error behaviour and async/event semantics.
4. Complete the XNet/XSocket APIs required by a selected title, backed by Winsock. Keep unimplemented features explicit (QoS, secure XNADDR/key exchange, NAT traversal, voice, broadcast/system-link and overlapped I/O).
5. Run Windows/MSVC x86 builds and tests. First prove UDP/TCP loopback through the Xbox ABI wrappers, then real LAN traffic, then a supported title's actual connection flow.

## Named XNET import binding

The x86 guest now resolves the XBE non-kernel import directory for the XNET / XNET.DLL library. The registry binds the 29 XNet/XSocket entry points currently implemented in xnet_compat.cpp to their actual 32-bit guest function addresses. The resolver validates descriptor, thunk, library-name and import-name ranges; accepts a name thunk stored as an image VA or RVA; temporarily makes each import-thunk cell writable and restores its prior page protection; and rejects unknown XNET names, unknown libraries and ordinal imports. It does not patch statically linked XNet call sites: only imports represented in the XBE non-kernel import directory are covered.

A dedicated x86 guest_api_registry_smoke target exercises a known named import and fail-closed cases for ordinal imports, unknown API names and unknown libraries. This test target has been added but has not yet been built or run in this environment.

## Tests required

- Startup/cleanup reference counting and failure paths.
- Guest handle validation, stale handles, socket limits and close races.
- TCP and UDP send/receive, bind/connect/listen/accept, select, nonblocking and timeouts.
- DNS using a configurable resolver and Windows fallback.
- XNADDR/link-status mapping from Windows IP Helper data.
- Guest ABI layout/calling convention checks for every exposed structure/function.
- A real XBE call-path test proving title networking calls reach the translation functions.

## Non-goals

- No MCPX/NVNet device model or PCI/MMIO/DMA/PHY emulation.
- No raw Ethernet frame bridge unless a future title demonstrably requires one.
- No Xbox Live/Insignia server implementation.
- No unverified XDK binary decompilation or arbitrary static-code patching.
## Dispatch audit — concrete blocker

A source audit of `tools/kernel_exports.json` against `guest32/kernel_compat.cpp` found that the guest's kernel dispatcher currently has **no routes** for the I/O-manager entry points that would be candidates for a lower-level device-service boundary:

| Ordinal | Xbox kernel export | Current guest dispatcher |
|---:|---|---|
| 59–62 | `IoAllocateIrp`, `IoBuildAsynchronousFsdRequest`, `IoBuildDeviceIoControlRequest`, `IoBuildSynchronousFsdRequest` | Unsupported |
| 65–69 | `IoCreateDevice`, `IoCreateFile`, `IoCreateSymbolicLink`, `IoDeleteDevice`, `IoDeleteSymbolicLink` | Unsupported |
| 72–73 | `IoFreeIrp`, `IoInitializeIrp` | Unsupported |
| 77 | `IoQueueThreadIrp` | Unsupported |
| 81–87 | `IoStartNextPacket`, `IoStartNextPacketByKey`, `IoStartPacket`, `IoSynchronousDeviceIoControlRequest`, `IoSynchronousFsdRequest`, `IofCallDriver`, `IofCompleteRequest` | Unsupported |
| 196 | `NtDeviceIoControlFile` | Unsupported |
| 200 | `NtFsControlFile` | Unsupported |

This is a verified source-level gap, **not proof that retail XNet uses every one of these exports**. The current `KernelDispatch` sends unmapped ordinals to `K_Unsupported`, which raises a guest exception. Therefore, even if a title's statically linked networking code reaches one of these lower-level APIs, the current guest cannot service that call.

The guest import resolver now handles named XNET non-kernel imports through a bounded registry. It does not install hooks into statically linked XNet code, and it deliberately rejects unsupported libraries, names and ordinal imports. The XNet/XSocket functions are reachable only where the XBE exposes supported named imports.

### XBE import diagnostics added

The kernel thunk resolver now emits a debugger-output line when an XBE imports one of the candidate networking/device-service kernel exports audited above. The trace includes the ordinal and export name for I/O-manager candidates (59–87, selected entries), `NtDeviceIoControlFile` (196), `NtFsControlFile` (200), and the PHY exports (252–253).

This does not pretend to implement IRP/device semantics. It makes the next investigation evidence-driven: load a target XBE under a debugger, capture its `[XboxKernel] XBE imports network/I-O candidate` lines, and implement only the kernel calls the title actually imports. Static XNet call sites still require separate proof and are not patched by this diagnostic.

### Next safe engineering step

Do not invent ordinal aliases for XNet. Next, capture a real title's import directory and trace one selected title's networking path to determine whether it uses named XNET imports or statically linked calls. If static calls must be redirected, do so only with per-title instruction-boundary evidence and an opt-in hook. The named import registry is not proof that arbitrary retail titles reach Winsock.

Audit scope: `tools/kernel_exports.json` export names/ordinals compared with the `cmp eax, <ordinal>` routes in `KernelDispatch`; source inspection only. No Windows build or runtime test was run as part of this audit.

## First kernel-level Windows translation implemented

The guest kernel now routes the documented PHY exports to Windows adapter discovery:

- Ordinal 252, `PhyGetLinkState(BOOLEAN Verify)`: calls `GetAdaptersAddresses` and returns 1 when at least one host adapter reports `IfOperStatusUp`, otherwise 0. The Xbox `Verify` argument is accepted but ignored because Windows exposes current operational status rather than the Xbox PHY's MII repoll mode.
- Ordinal 253, `PhyInitialize(BOOLEAN ForceReset, PVOID Param2)`: validates that Windows adapter enumeration is available and returns `STATUS_SUCCESS` (0) on success/no-adapter state, or `STATUS_UNSUCCESSFUL` on an enumeration failure. Windows owns physical NIC initialization; the guest does not reset or access hardware.

This is a real kernel-import path, unlike merely exporting XNet wrapper names: the XBE kernel thunk resolver already maps imported kernel ordinals through `KernelDispatch`, and these two ordinals now have explicit routes. It provides host link-state visibility only; it does **not** implement packet transport by itself or prove that a particular title reaches Winsock. The existing XNet/XSocket Winsock backend remains the socket transport layer, and a real-XBE end-to-end call-path test is still required.

The ABI signatures and ordinals were checked against the Xbox kernel export reference. The host implementation uses Windows IP Helper (`GetAdaptersAddresses`), already linked by `guest32/CMakeLists.txt` as `iphlpapi`. No MCPX register, PHY, DMA, or interrupt model was added.
