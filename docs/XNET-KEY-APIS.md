# XNet random and system-link key APIs

XboxRuntime exposes four additional named XNET imports:

- `XNetRandom` fills a caller-provided buffer with bytes from the Windows system-preferred BCrypt random provider.
- `XNetCreateKey` creates a random 8-byte XNKID and 16-byte XNKEY. The generated XNKID is marked as a system-link identifier.
- `XNetRegisterKey` stores system-link key pairs in a bounded in-memory table. The table limit follows `XNetStartupParams::cfgKeyRegMax`.
- `XNetUnregisterKey` removes an entry and securely zeroes its key material.

The implementation rejects null pointers, use before `XNetStartup`, duplicate identifiers, unsupported online key identifier types, and registrations beyond the configured limit. Key material is zeroed on unregister and final XNet cleanup.

## Scope limits

This is local key generation and registration bookkeeping only. It does **not** implement the Xbox secure-session handshake, packet encryption/authentication, online XNADDR resolution, QoS, NAT traversal, or Insignia sign-in. The functions must not be interpreted as proof that system-link or online multiplayer is complete.

## Tests

The x86 `network_smoke` test checks random-buffer validation, key creation, system-link identifier type, duplicate registration rejection, configured table limits, and unregistration. The guest API registry smoke test checks that all four names resolve through the XBE named-import registry.
