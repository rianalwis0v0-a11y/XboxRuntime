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


## Asynchronous DNS and socket error behavior

`XNetDnsLookup` now returns an allocated result immediately with
`iStatus = WSAEINPROGRESS`, resolves on a worker thread, publishes the final
status/address list, and signals the supplied WSA event after publishing.
The worker holds an internal XNet/Winsock lifetime reference, so a title may
call `XNetCleanup` while a lookup is outstanding without shutting Winsock
down underneath the resolver. The title must wait for the event before reading
or releasing the returned `XNDNS` object.

The guest socket bridge now sets Winsock last-error values for invalid/stale
guest tokens, invalid buffers and lengths, unsupported socket types, and
unresolvable host names. Native Winsock failures retain their native error
codes.

The networking smoke test covers asynchronous localhost DNS completion,
TCP loopback listen/connect/accept/stream I/O, UDP loopback datagrams, stale
socket token errors, key registration lifecycle, and guest import registration.


## XSocket readiness and error APIs

`XSocketSelect` accepts guest socket tokens in its input `fd_set` values.
The bridge validates each token, leases its native socket for the duration of
the wait, translates the sets to native Winsock handles, calls `select`, and
translates the ready sets back to guest tokens. This prevents guest tokens from
being mistaken for host socket values and prevents a concurrent close from
destroying a socket during the wait.

`XSocketGetLastError` and `XSocketSetLastError` expose the current thread's
Winsock error state. Invalid handles, unsupported socket types, malformed
addresses, and invalid arguments set explicit Winsock errors.

The TCP smoke test now checks listener readability through `XSocketSelect`
before accepting the client, and verifies the XSocket last-error accessors.


## Explicit Insignia resolver configuration and unsupported mappings

The transport does not hard-code an Insignia DNS server. An optional resolver
address can be supplied through `XBOX_RUNTIME_INSIGNIA_DNS_IPV4`. If the
variable is absent or invalid, hostname resolution uses the Windows-configured
resolver. This allows a local Insignia DNS responder to be selected explicitly
without changing the host's global DNS settings.

`XNetServerToInAddr` and `XNetInAddrToServer` currently return
`WSAEOPNOTSUPP`: the service-ID address translation table is not implemented.
They deliberately do not return the input address as a fake successful mapping.
