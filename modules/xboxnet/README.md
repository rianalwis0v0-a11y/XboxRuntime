# XboxNet — XboxRuntime networking subsystem

XboxNet is the networking compatibility layer for the 32-bit original-Xbox guest.
It is being developed independently on the `xboxnet` branch.

## Current implementation

The guest-side layer is split into two parts:

- `guest32/network_compat.*`: host Winsock transport with 32-bit guest socket
  tokens, IPv4 DNS resolution, TCP/UDP operations, and safe socket-token
  lifecycle management.
- `guest32/xnet_compat.*`: an initial original-Xbox XNet/XSocket-compatible
  API tranche, including startup/cleanup, key registration for system-link
  identifiers, asynchronous DNS completion, random bytes, basic address/status
  helpers, WSA event wrappers, and core TCP/UDP socket calls.

The guest ABI is explicitly 32-bit. A host `SOCKET` value is never exposed as a
guest socket token. Fixed-width XNet structures have compile-time size/offset
assertions.

## Build and smoke test

Run from this directory with Visual Studio 2022 installed:

```powershell
cmake -S guest32 -B build/xboxnet -G "Visual Studio 17 2022" -A Win32
cmake --build build/xboxnet --config Release
ctest --test-dir build/xboxnet -C Release --output-on-failure
```

The standalone project is intentionally limited to the networking smoke test;
it does not pretend to build the complete XboxRuntime tree from this branch.

## Not implemented / not proven

- This is not a complete XDK networking implementation.
- QoS, secure online address/key handshakes, NAT traversal, voice networking,
  broadcast/system-link discovery, and overlapped I/O remain incomplete.
- No non-kernel XBE import resolver or verified static XNet interception is
  wired into this branch yet. A title will not automatically call these
  routines merely because the functions compile.
- The resolver's Insignia-first DNS path is a compatibility option, not proof
  of service reachability or sign-in.
- No real Xbox title has completed an Insignia sign-in through this code.
- No Windows/MSVC build has been run by this session; CI evidence is required.

Do not advertise Xbox Live compatibility until a supported, unmodified title
has completed a real connection/sign-in flow and the traffic has been captured
and inspected.
