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

## Main blocker: making retail XBE networking calls reach the translator

XNet is commonly statically linked into retail titles. Merely exporting functions named XNetStartup or XSocketSend from the guest executable does not automatically redirect a title's already-linked internal calls to those functions.

The XBE loader currently parses and passes through the non-kernel import-directory address, while the execution backend owns import interpretation. The next task is to trace the backend's XBE import/thunk processing and determine a **verified** interception point for networking calls. Do not guess import ordinals, patch arbitrary byte patterns, or claim compatibility until the actual call path is identified.

Preferred implementation order:

1. Audit the x86 guest's kernel import resolver and backend interface. Identify which Xbox networking-related kernel exports are actually imported by the title and what ABI they use.
2. Verify how statically linked XNet reaches lower-level kernel services in a real XBE. Use NXDK/public symbol references and controlled XBE fixtures; distinguish documented facts from hypotheses.
3. Add a narrow, opt-in translation hook at a verified boundary. Preserve original calling conventions, guest memory validation, return codes, last-error behaviour and async/event semantics.
4. Complete the XNet/XSocket APIs required by a selected title, backed by Winsock. Keep unimplemented features explicit (QoS, secure XNADDR/key exchange, NAT traversal, voice, broadcast/system-link and overlapped I/O).
5. Run Windows/MSVC x86 builds and tests. First prove UDP/TCP loopback through the Xbox ABI wrappers, then real LAN traffic, then a supported title's actual connection flow.

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