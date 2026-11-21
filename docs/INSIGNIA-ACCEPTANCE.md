# Windows-hosted Xbox API compatibility harness

## Architecture rule

XboxRuntime is a Windows harness for running Original Xbox XBE code. It preserves
the 32-bit Xbox-facing ABI at the guest boundary and translates supported calls
to native Windows functionality underneath.

- **Emulated Xbox-specific state:** the 256-byte EEPROM, persisted to the
  configured host file. A blank EEPROM remains unprovisioned; the runtime must
  not invent a console identity.
- **Host-owned networking:** Windows adapter enumeration, DNS resolution,
  TCP/UDP sockets, packet transport, and socket lifecycle use Windows APIs
  (IP Helper and Winsock). Do not emulate an Xbox NIC, MCPX/NVNet hardware,
  Ethernet controller, TCP/IP stack, or packet path.
- **API compatibility:** Xbox kernel/XNet/XSocket entry points retain their
  guest calling convention, structure layout, and 32-bit pointer/handle model.
  Their implementations translate to Windows APIs where an equivalent exists.
- **Service behavior:** Insignia or another Xbox service is an external endpoint.
  The harness must not hard-code service DNS addresses or implement a service
  server as part of the Windows transport layer.

The guiding rule is: emulate only the Xbox-specific state that Windows cannot
supply (currently the EEPROM); use Windows for the actual host capabilities.

## Current implementation

### Windows networking backend

The guest32/network_compat.cpp file provides:

- Winsock 2.2 startup/cleanup with reference counting.
- Host-configured DNS through getaddrinfo, inheriting Windows resolver policy,
  DNS servers, and search suffixes.
- IPv4 TCP and UDP socket creation and transport.
- Bind/connect/listen/accept, send/receive, sendto/recvfrom, shutdown,
  nonblocking mode, socket options, select, and timeouts.
- 32-bit guest socket tokens mapped to native Windows SOCKET handles.
  Native host handles are not exposed as guest values.
- Socket leases so an in-flight operation can finish safely while a guest
  closes its token.

The guest32/xnet_compat.cpp file provides an initial Xbox-facing XNet/XSocket ABI
layer over that backend, including startup/cleanup, DNS lookup, address
helpers, link state, and core socket operations.

### EEPROM

XboxKernelInitializeEeprom loads or creates a persistent 256-byte EEPROM
image. A newly created image is all 0xFF and explicitly unprovisioned.
SMBus reads and writes access this image, and writes are persisted to the
configured host file. Invalid-size existing EEPROM files are rejected rather
than silently truncated or replaced.

### XBE import compatibility

The guest import resolver handles Xbox kernel thunk imports. The separate
named-import registry resolves a supported set of XNET/XNET.DLL imports
to guest ABI thunks. This is a compatibility bridge, not proof that every
XNet export or every statically linked XNet call site is covered.

Static-XNET instrumentation identifies executable XBE sections named XNET
and traces selected kernel imports called from those ranges. It is diagnostic
instrumentation only: it does not detour arbitrary statically linked XNet
functions or itself implement the network dataplane.

## Validation

The networking validation workflow builds the x86 guest and runs:

- XBE named-import registry smoke tests.
- Static-XNET trace smoke tests.
- Host Winsock startup, DNS, TCP/UDP, and XSocket-wrapper smoke tests.

The main Windows build also runs the native x86 XBE execution test and a
comprehensive diagnostic XBE. A passing build or loopback test is an
intermediate check, not proof that a retail title can sign in to Insignia.

Final acceptance requires an unmodified supported title to execute its Xbox
networking code through the compatibility boundary, resolve service names
using the host's configured DNS path, and exchange real traffic with the
intended external service. Multiplayer additionally requires real peer UDP
traffic and NAT behavior to be validated.

## Known limitations

- The XNet/XSocket API surface is incomplete; QoS, secure address/key
  registration, NAT traversal, voice encryption, broadcast/system-link
  behavior, overlapped I/O, and other title-specific exports still need work.
- Static-XNET trace coverage depends on a discoverable executable section
  named XNET; titles that fold static code into generic sections may not
  be identified by this instrumentation.
- The comprehensive diagnostic currently fails its synthetic static-XNET
  NtOpenFile config-path case (observed status 0xC0000033). The host x86
  guest build and networking validation workflow pass, but this failure must
  be diagnosed before the branch can be called fully validated.
- No successful retail-title Insignia sign-in or multiplayer acceptance run
  has been recorded yet.

## Non-goals

- No Xbox Ethernet/NIC hardware emulation.
- No emulated Xbox TCP/IP or packet stack.
- No hard-coded Insignia DNS resolver in the transport backend.
- No Xbox Live/Insignia server implementation inside this compatibility layer.
- No fabricated EEPROM identity or automatic console provisioning.
