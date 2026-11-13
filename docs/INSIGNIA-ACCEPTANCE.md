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
