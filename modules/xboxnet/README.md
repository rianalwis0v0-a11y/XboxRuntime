# XboxNet — XboxRuntime networking subsystem

XboxNet is the networking compatibility layer for the 32-bit original-Xbox guest,
plus server-side infrastructure for a community-run online service.

## Guest networking

- `guest32/network_compat.*`: host Winsock transport with 32-bit guest socket
  tokens, IPv4 DNS resolution, TCP/UDP operations, and safe socket lifecycle.
- `guest32/xnet_compat.*`: an initial original-Xbox XNet/XSocket-compatible API
  tranche, including startup/cleanup, system-link identifiers, async DNS, random
  bytes, address/status helpers, WSA event wrappers, and TCP/UDP calls.

## XboxNet DNS

`dns_server.py` is a mapping DNS server inspired by the public Insignia DNS
implementation. It supports local A/AAAA records and an optional JSON zone feed
with Insignia-style `a` (static IPv4) and `p` (hostname-resolved IPv4) entries.
It listens on UDP and TCP, periodically refreshes the feed, and returns
NOERROR/NODATA for unmapped names instead of acting as an open recursive resolver.

Run locally:

```sh
python3 dns_server.py --config xboxnet-dns.example.json --bind 127.0.0.1 --port 5353
python3 -m unittest discover -s tests -v
```

For real Xbox clients, use a reachable host and port 53, allow UDP/53 and TCP/53
through its firewall, and protect the service from unwanted public use. The
dashboard DNS field takes an IP address. DuckDNS can track a changing public IP
but cannot bypass CGNAT or create port forwarding.

The example address is reserved for documentation and is not a live XboxNet
endpoint. Do not configure an Xbox to use it as a production service.

## HTTP status gateway

`gateway.py` is the first server-side HTTP component. It is intentionally
read-only and binds to loopback by default:

```sh
python3 gateway.py --bind 127.0.0.1 --port 8081
```

Available routes:

- `GET /healthz` — process health.
- `GET /v1/status` — XboxNet status and implementation boundaries.
- `GET /v1/services` — service catalog.
- Unknown paths return 404. POST requests return 501 because no XboxNet
  protocol handlers or state-changing APIs have been implemented.

The catalog lists authentication/domain, security gateway, profiles,
friends/presence, title-specific matchmaking, and peer connectivity as
`not_implemented`. These are architecture placeholders, not working Xbox Live
services. The gateway does not accept console authentication or game traffic.
Do not expose it publicly as an Xbox Live endpoint.

Gateway tests are included in the existing Python test suite:

```sh
python3 -m unittest discover -s tests -v
```

## Architecture research

See [docs/INSIGNIA-ARCHITECTURE-NOTES.md](../../docs/INSIGNIA-ARCHITECTURE-NOTES.md)
for the public Insignia DNS design and the high-level original Xbox Live service
architecture. DNS is only discovery; authentication, the security gateway, HTTP
service APIs, and title-specific matchmaking are separate backend work.

## Guest build and smoke test

Run from the repository root with Visual Studio 2022:

```powershell
cmake -S guest32 -B build/xboxnet -G "Visual Studio 17 2022" -A Win32
cmake --build build/xboxnet --config Release
ctest --test-dir build/xboxnet -C Release --output-on-failure
```

No real Xbox title has completed an XboxNet sign-in through this code. Do not
advertise Xbox Live compatibility until a supported, unmodified title completes
a real sign-in and game connection with captured traffic inspected.
