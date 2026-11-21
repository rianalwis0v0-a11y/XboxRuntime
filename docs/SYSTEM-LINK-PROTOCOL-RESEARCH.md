# Original Xbox System Link protocol research

Status: research only; no wire-protocol implementation is claimed by this document.

## Confirmed observations

The original Xbox System Link transport is not ordinary UDP game traffic with a title-defined payload placed directly after the UDP header.

- The well-known transport port is UDP 3074.
- Captures from Halo: Combat Evolved show the console using source address `0.0.0.1` and sending discovery traffic to `255.255.255.255`. Ethernet MAC addresses distinguish the participating consoles in the observed LAN setup.
- The captured broadcast payload begins with eight `0xff` bytes followed by ciphertext-like data.
- The Halo investigation observed a 60-byte discovery request about every two seconds and a 324-byte host response. These are Halo observations, not a guarantee that every title uses identical discovery timing or payloads.
- The reverse-engineering article traced the kernel crypto path through `XcKeyTable` and `XcBlockCryptCBC`. It experimentally confirmed Triple-DES CBC for the investigated Halo traffic, including a 24-byte key and 8-byte IV. This is evidence for that protocol path, not a license to assume every XNet packet uses the same framing.

## Key derivation and security notes

The XboxDevWiki System Link notes describe derivation from the kernel `XboxLANKey` and the game-specific LAN key embedded in the XBE certificate. The documented construction uses HMAC-SHA1-derived material, then derives SHA and DES key material (including DES parity adjustment). The wiki itself marks parts of the protocol as incomplete.

Do not confuse:
- `XNetCreateKey` / `XNetRegisterKey` session-key bookkeeping,
- the title's certificate LAN key and kernel `XboxLANKey`,
- the per-packet security association and encrypted transport.

The current XboxRuntime key registry only implements the first item. It does not yet derive the original LAN traffic keys, build security associations, authenticate/replay-check packets, or encrypt/decrypt XSP traffic.

## Implementation consequences for XboxRuntime

1. Keep the current UDP socket bridge as a generic host transport only. It is not yet an original-Xbox System Link implementation.
2. Do not send raw title payloads to UDP 3074 and call that compatible System Link.
3. Do not invent a new packet header, nonce scheme, or cipher mode and label it XNet-compatible.
4. The next implementation step requires a separately reviewed protocol specification and tests for:
   - broadcast discovery framing and response parsing,
   - XSP packet header, SPI, sequence-number, IV, and authentication rules,
   - LAN key derivation from the actual XBE certificate and kernel key,
   - per-peer security association lifecycle and replay protection,
   - unicast session traffic and broadcast exceptions.
5. Preserve an explicit compatibility boundary: unsupported encrypted System Link traffic should fail closed rather than be silently passed through as if it were authenticated.

## References

- [Exploring the Halo 1 System Link Protocol](https://hllmn.net/blog/2023-09-18_h1x-net/) — packet captures, 0.0.0.1/UDP 3074 observations, kernel crypto tracing, and experimental Triple-DES-CBC confirmation.
- [XboxDevWiki: System Link](https://xboxdevwiki.net/System_Link) — summary of UDP 3074, LAN key derivation, broadcast messages, and security-association notes; page explicitly contains unfinished sections.
- [IANA Service Name and Transport Protocol Port Number Registry](https://www.iana.org/assignments/service-names-port-numbers) — registers Xbox on TCP and UDP port 3074.

## Scope and confidence

The Halo article is a detailed reverse-engineering write-up of one title and one observed System Link path. The XboxDevWiki page is useful secondary documentation but has FIXME markers. Together they provide a grounded starting point, not a complete normative XNet specification. Full interoperability must be validated against captures from multiple titles and real Xbox hardware or a known-compatible emulator.
