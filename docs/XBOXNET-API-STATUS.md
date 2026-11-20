# XboxNet API coverage ledger

This ledger describes the implementation currently present on the `xboxnet`
branch. “Implemented” means a code path exists; it does not mean the Xbox ABI
has been independently verified against every title.

| API area | Current state | Evidence / limitation |
|---|---|---|
| Winsock lifecycle | Implemented | Reference-counted WSAStartup/WSACleanup wrapper and transport startup. |
| IPv4 resolver | Partial | Insignia-first DNS A lookup for fully-qualified names, then OS resolver fallback. Not yet configurable. |
| TCP sockets | Initial implementation | Create, bind, connect, listen, accept, send, receive, shutdown, name queries, options, timeout and nonblocking helpers. |
| UDP sockets | Initial implementation | Create, bind, sendto, recvfrom and socket options. |
| XSocket handles | Implemented token model | Guest-visible handles are 32-bit tokens, not native host SOCKET values. |
| XNet startup/cleanup | Initial implementation | Validates the 12-byte startup structure and applies defaults. |
| XNet DNS | Initial implementation | Worker-thread completion and event signaling; result release is synchronized with in-flight work. |
| XNet random | Implemented via OS CSPRNG | Uses BCrypt system-preferred random provider. |
| XNKID/XNKEY system-link registration | Partial | Key creation and local system-link registration table; no online security handshake. |
| XNet title address | LAN-only | Reports a usable local IPv4 adapter; does not fabricate online address data. |
| XNet address conversion | Unsupported | Online XNADDR/key mapping and service-ID lookup return WSAEOPNOTSUPP. |
| QoS / NAT / VDP voice | Not implemented | No QoS probing, NAT traversal, voice encryption or relay. |
| Overlapped I/O | Not implemented | No completion-port/overlapped socket contract yet. |
| Retail XBE integration | Not implemented on this branch | No verified non-kernel import binding or static XNet interception is wired up. |
| Insignia sign-in | Not demonstrated | No retail title has completed a live sign-in using this branch. |

## Next implementation order

1. Get the x86 Windows smoke target green in CI.
2. Compare public declarations and structure layouts against pinned XDK/NXDK
   headers, recording source revisions and correcting any mismatch.
3. Add error-path tests for invalid socket tokens, stale handles, invalid
   sockaddr lengths, socket limits, and cleanup racing with operations.
4. Agree with the XBE-loader owner on a versioned non-kernel import/interception
   contract before wiring title imports.
5. Implement and test online address/key registration and service discovery
   only after the required protocol behavior is documented.
6. Capture real traffic from a supported title and verify sign-in end to end.
