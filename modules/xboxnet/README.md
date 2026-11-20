# XboxNet — XboxRuntime networking subsystem

XboxNet is the networking compatibility layer for the 32-bit original-Xbox guest,
plus the server-side infrastructure being built to support XboxNet services.

## Guest networking

The guest-side layer is split into two parts:

- `guest32/network_compat.*`: host Winsock transport with 32-bit guest socket
  tokens, IPv4 DNS resolution, TCP/UDP operations, and safe socket-token lifecycle.
- `guest32/xnet_compat.*`: an initial original-Xbox XNet/XSocket-compatible API
  tranche, including startup/cleanup, system-link identifiers, asynchronous DNS,
  random bytes, basic address/status helpers, WSA event wrappers, and TCP/UDP calls.

The guest ABI is explicitly 32-bit. Host socket values are never exposed as guest
socket tokens. Fixed-width XNet structures have compile-time size/offset checks.

## XboxNet DNS server

`dns_server.py` is the first server-side component. It listens on UDP and TCP,
answers configured A/AAAA records, and forwards other DNS questions to an upstream
resolver. It defaults to port 5353 for safe local testing and has no third-party
Python dependencies.

Run from this directory:

```sh
python3 dns_server.py --config xboxnet-dns.example.json --bind 127.0.0.1 --port 5353
python3 -m unittest discover -s tests -v
```

For deployment, use a reachable host and port 53, permit both UDP/53 and TCP/53
through its firewall, and restrict access so it does not become an open public
resolver. The original Xbox dashboard's **DNS Server** field takes the reachable
DNS server IP address, not a DuckDNS hostname. DuckDNS can track a changing public
IP but cannot bypass CGNAT or configure router port forwarding.

The example record uses a reserved documentation IP and is not a real XboxNet
service endpoint. The final Xbox Live hostname map and service endpoints have not
been defined yet.

## Guest build and smoke test

Run from the repository root with Visual Studio 2022:

```powershell
cmake -S guest32 -B build/xboxnet -G "Visual Studio 17 2022" -A Win32
cmake --build build/xboxnet --config Release
ctest --test-dir build/xboxnet -C Release --output-on-failure
```

The standalone guest project is intentionally limited to the networking smoke
test; it does not build the complete XboxRuntime tree.

## Not implemented / not proven

- This is not a complete XDK networking implementation.
- QoS, secure online address/key handshakes, NAT traversal, voice networking,
  broadcast/system-link discovery, and overlapped I/O remain incomplete.
- The DNS server is an early forwarder, not a complete Xbox Live service directory.
  It has no DNSSEC validation, caching, zone transfers, or dynamic updates.
- The resolver's Insignia-first DNS path is not proof of service reachability or
  sign-in. XboxNet's final hostname mapping still needs protocol research.
- No real Xbox title has completed an XboxNet sign-in through this code.
- CI must pass before either guest networking or the DNS server is considered
  verified on its target platform.

Do not advertise Xbox Live compatibility until a supported, unmodified title has
completed a real connection/sign-in flow and the traffic has been captured and
inspected.
