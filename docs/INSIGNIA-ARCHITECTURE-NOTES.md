# XboxNet DNS and original Xbox Live architecture notes

## What the public Insignia material establishes

The Insignia team's public `insigniaDNS` repository shows a DNS mapping service,
not a complete Xbox Live backend. Its client downloads a JSON zone list from
`https://insignia.live/dns_zones.json`, then answers mapped DNS names locally.
The published feed demonstrates two record modes:

- `a`: return the supplied IPv4 address.
- `p`: resolve the supplied hostname to an IPv4 address.

The reference implementation listens on UDP and TCP port 53. It does not forward
unmapped questions to a public recursive resolver; it returns an empty DNS reply
when it has no matching zone. The dashboard can use a separate secondary DNS
resolver for ordinary names.

A public interview with the Insignia team describes the larger original Xbox
Live service at a high level: custom Kerberos-based authentication, a separately
implemented security gateway, HTTP-based services behind that gateway, and
mostly peer-to-peer game traffic. The centralized service handles functions
such as authentication, matchmaking, and friends lists. This is an architectural
overview, not a published wire-protocol specification.

## What this means for XboxNet

DNS is only the routing/discovery front door. A working DNS answer does not make
an Xbox Live title sign in. XboxNet needs distinct backend services and protocol
implementations, with DNS records pointing to endpoints that XboxNet actually
operates.

Recommended service boundaries:

1. **DNS/zone directory** — authoritative mappings for the Xbox Live hostnames
   XboxNet chooses to support. Never point a name at an endpoint that is not live.
2. **Authentication/domain service** — research and implement the original Xbox's
   Kerberos-derived protocol and its console/account identity checks.
3. **Security gateway** — validate and route authenticated service traffic.
4. **HTTP service APIs** — implement observed request/response behavior for
   registration, account/profile, and friends/presence.
5. **Title-specific matchmaking** — implement per-title behavior based on
   captures and controlled tests. Keep actual gameplay traffic peer-to-peer
   where the title's protocol expects it.

The exact endpoint names, packet fields, key derivation, ticket formats, and
gateway framing must be established from public documentation, permitted source
code, or traffic from a console the developer owns. They must not be guessed.

## Current XboxNet implementation status

- The DNS server supports local A/AAAA mappings and Insignia-style JSON zone-feed
  entries (`a` static IPv4 and `p` hostname-resolved IPv4).
- It serves both UDP and TCP and refreshes a configured feed periodically while
  retaining the last known-good table if refresh fails.
- Unmapped names return NOERROR/NODATA, matching the reference mapping-server
  behavior rather than acting as an open recursive resolver.
- The example uses a reserved TEST-NET address. It is not a live service.
- `modules/xboxnet/auth_domain.py` now defines an internal, protocol-neutral
  verifier interface with size bounds and fail-closed behavior. Its default
  verifier rejects all tickets; it is only an adapter seam, not an Xbox endpoint.
- XboxNet still does not implement the Kerberos-derived ticket format, key
  derivation, domain exchange, security gateway, account APIs, or matchmaking
  backend. No Xbox Live sign-in should be claimed.

## References

- Insignia DNS source: https://github.com/insignia-live/insigniaDNS
- Insignia live DNS zone feed: https://insignia.live/dns_zones.json
- Insignia connection guide: https://insignia.live/connect
- Public interview describing the service architecture:
  https://www.gamespot.com/articles/the-og-xbox-live-is-back-from-the-dead-this-is-the-team-making-it-happen/1100-6515605/
