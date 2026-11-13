# Insignia connectivity acceptance plan

## Goal

XboxRuntime must run a supported original-Xbox title in the x86 guest and let its
original Xbox Live code reach the real Insignia service. A successful build,
a DNS lookup alone, or a mocked socket is not a passing result.

Insignia's documented normal setup uses primary DNS 46.101.64.175 and a
public secondary resolver such as 8.8.8.8 or 1.1.1.1. Console registration
and title support are separate prerequisites.

## Test stages

1. **Host transport**
   - Guest UDP and TCP sockets can send and receive real traffic through the
     Windows host.
   - DNS queries resolve through the configured resolver.
   - TCP connect, timeout, shutdown, close, and error paths are tested.
   - No test silently substitutes loopback or a fake server.

2. **Xbox API compatibility**
   - Implement the XNet and XSocket exports actually imported by retail XBEs.
   - Match Xbox calling convention, structure layout, byte order, return values,
     asynchronous completion behavior, and error translation.
   - Keep guest pointers and handles inside the guest address/handle model;
     never pass a guest pointer directly to a 64-bit host API.
   - Validate import ordinals against known Xbox kernel/XNet import definitions
     before adding dispatch entries. Do not guess ordinal values.

3. **Address and service discovery**
   - Implement XNet startup/cleanup and Xbox address structures.
   - Support hostname resolution and the XNet address conversion routines
     required by the selected title.
   - Preserve the configured DNS path; do not hard-code Insignia server IPs
     into individual API stubs.

4. **Connection test**
   - Run an unmodified, supported original-Xbox title or Insignia Setup
     Assistant in the guest.
   - Capture the DNS and connection sequence.
   - Confirm the guest reaches the real Insignia endpoints and receives
     protocol-valid replies.
   - Record the exact failing stage if the test does not complete.

5. **Multiplayer**
   - Verify peer-to-peer UDP traffic, NAT behavior, and title session discovery.
   - A successful service sign-in is not by itself proof that multiplayer works.

## Current repository audit

- `modules/insignia_dns` is a host-side DNS forwarding/hijacking helper. It
  is not an implementation of the Xbox XNet/XSocket APIs.
- The helper binds UDP and TCP port 53 and therefore has host privilege,
  port-conflict, and lifecycle considerations.
- `main/main.cpp` currently starts NV2A, FATX, input, XDVDFS, and XBE loader;
  it does not load `insignia_dns.dll`.
- The x86 guest's kernel import resolver currently dispatches kernel ordinals;
  no verified XNet/XSocket compatibility layer has been found in the audited
  files yet.
- The current guest process is a separate 32-bit process. Networking must be
  implemented at that guest boundary, not by exposing host pointers as guest
  API parameters.

## Pass criteria

The milestone is **PASS** only when a real supported title in the x86 guest
completes the Insignia connection/sign-in flow using real network traffic.
DNS-only success, a successful Winsock startup, stubbed exports, and compile
success are intermediate checks, not acceptance.


## Networking implementation progress

The first guest-side transport foundation is now present in `guest32/network_compat.cpp`:

- Winsock 2.2 startup with reference-counted initialization and cleanup.
- IPv4 hostname resolution through the Windows resolver.
- TCP and UDP socket creation.
- TCP/connected-UDP connect, bind, listen, accept, send, receive, and socket timeouts.
- 32-bit guest tokens map to host `SOCKET` values; raw host socket handles are never exposed as guest tokens.
- Closing a guest token removes it from the handle table before closing the host socket.

A `network_smoke` CTest target checks Winsock startup, resolving `localhost`, creating TCP/UDP sockets, and closing them.

**Important limitation:** this is a transport backend, not yet the Xbox XNet/XSocket ABI. The XBE loader currently resolves the Xbox kernel thunk table only; it does not yet parse non-kernel import libraries and bind XNet/XSocket imports to these functions. Therefore this commit does not make Insignia sign-in or multiplayer work yet. The next required step is implementing the XBE non-kernel import resolver and the correct Xbox XNet/XSocket ABI/structure mappings, then running a real title against Insignia.


## Insignia-first DNS resolution (guest transport)

The guest transport resolver now sends an IPv4 DNS A query for fully qualified
hostnames to Insignia's documented primary resolver, `46.101.64.175`, before
falling back to the Windows resolver. This avoids relying on an ISP's possibly
intercepted DNS path for Xbox service hostnames. Loopback, `.local`, and
single-label names stay on the normal resolver. Replies are checked for the
expected transaction ID, source server, response flag, and successful RCODE;
only an IPv4 A record is accepted.

This does **not** redirect arbitrary HTTP requests to the Insignia website, and
it does not forward ICMP ping packets. DNS changes only the IP address returned
for a hostname; subsequent TCP/UDP traffic must still be implemented by the
guest networking APIs. The resolver is currently used by the internal
`XboxNetResolveIPv4` / socket-connect bridge, not by every statically linked
XNet routine in retail titles. Full title integration still depends on the
verified XNet/XSocket interception and ABI work above.

Build/runtime status: implementation committed, but no Windows/MSVC build or
live DNS query test has been run in this environment.


## XNet/XSocket compatibility layer (initial API tranche)

Added guest-side ABI declarations and implementations in
`guest32/xnet_compat.h/.cpp`, based on documented/original-Xbox-compatible
structure definitions and cross-checked against public XNet/XSocket
implementations. The current tranche includes:

- `XNetStartup` / `XNetCleanup` with the 13-byte startup-parameter layout,
  reference counting, and Winsock transport lifecycle.
- `XNetDnsLookup` / `XNetDnsRelease` with the 40-byte `XNDNS` result layout.
  Lookup currently completes synchronously and signals the supplied WSA event.
  It uses the guest resolver, which tries Insignia DNS first for fully qualified
  names.
- Address conversion helpers and an Ethernet-link status placeholder.
- Core `XSocket` create/close, bind/connect/listen/accept, shutdown, nonblocking,
  send/receive, sendto/recvfrom, socket options, local/peer address queries, and
  byte-order helpers. Guest-visible socket values are 32-bit tokens, not raw
  host `SOCKET` handles.

The `network_smoke` target now includes an XNet startup/DNS-event check and a
real UDP loopback send/receive test through the XSocket wrapper.

**Not complete:** This is an initial API tranche, not a full XDK port. QoS,
secure address/key registration, NAT traversal, VDP voice encryption,
broadcast/system-link emulation, overlapped I/O, and the remaining XNet/XSocket
functions are not implemented. The current XBE loader still does not hook
statically linked XNet/XSocket code inside retail titles, so these routines
are not yet automatically invoked by arbitrary XBEs. A passing build and
runtime test are still required; no Windows/MSVC build has been run here.
