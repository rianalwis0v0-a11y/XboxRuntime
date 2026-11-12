# Built-in Insignia DNS

XboxRuntime includes `insignia_dns.dll`, a native Windows DNS responder based on
the behavior of the official [insigniaDNS](https://github.com/insignia-live/insigniaDNS)
project. The original project is MIT-licensed; its copyright and license must
be preserved if code is copied from it. This module is an independent C++
implementation of the documented zone format and behavior.

## How it works

- Downloads the current zone list from `https://insignia.live/dns_zones.json`
  over HTTPS using WinHTTP.
- Understands the official `a` (literal IPv4) and `p` (resolve another
  hostname to IPv4) zone records.
- Refreshes the zone list every five minutes.
- Answers matching A and ANY queries with a 300-second TTL.
- Listens on both UDP and TCP port 53 on all IPv4 interfaces.
- Returns an empty successful DNS answer for names not present in the Insignia
  zone list, matching the official resolver's behavior. It is not a general
  purpose recursive DNS server.

The resolver starts in offline mode if the zone download fails and retries in
the background. If port 53 is already occupied or cannot be bound, DNS is
disabled without preventing the rest of XboxRuntime from starting.

## Using it

For another original Xbox on the same LAN, set its primary DNS to the LAN IPv4
address of the Windows PE machine running XboxRuntime. Set secondary DNS to
`1.1.1.1` (the same fallback used in the official guide). Allow inbound UDP
and TCP port 53 through the host firewall.

For a recompiled XBE running inside XboxRuntime, the network compatibility
layer must supply the local resolver address (127.0.0.1) in its emulated
network configuration. Do not change the host's global DNS settings.

## Important limitations

This is the DNS redirection component only. It does not implement Insignia
account registration, Kerberos authentication, matchmaking, or game servers.
Those are provided by Insignia's remote service. Console registration and
other setup requirements still apply.

The current implementation handles IPv4 A/ANY records from the published
zone list. It does not implement general recursive forwarding, IPv6 AAAA
records, DNSSEC, or EDNS response expansion. Keep the public secondary DNS
configured as a fallback.

## Official setup references

- [Insignia connection guide](https://insignia.live/guide/connect)
- [Insignia DNS source](https://github.com/insignia-live/insigniaDNS)
- [Published DNS zones](https://insignia.live/dns_zones.json)
