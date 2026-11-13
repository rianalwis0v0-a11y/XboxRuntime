# Guest XNet/XSocket API manifest (preliminary)

Status: implementation inventory for kernel/guest-runtime integration; **not an ABI certification**.
Branch: `work/xbox-networking-integration`
Purpose: identify the callable functions currently compiled into the dedicated 32-bit guest target and the exact declarations in `guest32/xnet_compat.h`.

## Exposure model

These are C-linkage functions compiled into `xbe_guest32`; they are **not currently a separately exported xnet.dll or wsock32.dll**, and this does not make them callable from arbitrary retail XBEs by itself. The runtime still needs a verified guest-call registration/thunk mechanism. Do not assign kernel ordinals to statically linked XNet routines. The kernel/loader should only register identities that are actually present in a verified import table; static XNet call sites need a separate, reviewed interception strategy.

## Calling convention and process ABI

- Guest target: Win32 / x86; `sizeof(void*) == 4` is enforced by `guest32/CMakeLists.txt`.
- Title-facing declarations in `xnet_compat.h`: `WINAPI` (stdcall on x86).
- Internal `XboxNet*` bridge: `__cdecl`; these are implementation details, not title-facing exports.
- `SOCKET`, `DWORD`, `ULONG`, pointers and handles are 32-bit in this guest build.
- IPv4 addresses in `IN_ADDR` and `sockaddr_in.sin_addr` are network byte order; ports in `sockaddr_in.sin_port` are network byte order.
- Socket handles returned by `XSocketCreate` are 32-bit opaque guest tokens, not host SOCKET values.
- Structures use `#pragma pack(push, 4)` in the current header. The static assertions currently enforce `XNetStartupParams=13`, `XNADDR=36`, `XNDNS=40` bytes. These sizes still require comparison against an authoritative Xbox SDK-compatible header before claiming retail ABI compatibility.

## Implemented title-facing functions

| Function | Current declaration / arguments | Return and current behaviour |
|---|---|---|
| `XNetStartup` | `int WINAPI (const XNetStartupParams*)` | 0 on startup success; otherwise Winsock error. Reference-counted Winsock 2.2 startup. |
| `XNetCleanup` | `int WINAPI ()` | 0; decrements startup reference count and cleans up transport at zero. |
| `XNetDnsLookup` | `int WINAPI (const char*, WSAEVENT, XNDNS**)` | 0 when request is processed; result status is in `XNDNS.iStatus`. **Currently synchronous**, then signals the event; not yet true asynchronous completion. |
| `XNetDnsRelease` | `int WINAPI (XNDNS*)` | 0; frees result allocated by this implementation. |
| `XNetInAddrToString` | `int WINAPI (IN_ADDR, char*, int)` | 0 on success; WSA-style error on invalid/short output buffer. |
| `XNetServerToInAddr` | `int WINAPI (IN_ADDR, DWORD, IN_ADDR*)` | 0; currently a pass-through placeholder, not Xbox service-ID resolution. |
| `XNetInAddrToServer` | `int WINAPI (IN_ADDR, IN_ADDR*)` | 0; currently a pass-through placeholder, not online address conversion. |
| `XNetGetEthernetLinkStatus` | `DWORD WINAPI ()` | Host adapter-derived flags. Bit definitions and Xbox equivalence are **provisional and require ABI verification**. |
| `XNetGetTitleXnAddr` | `DWORD WINAPI (XNADDR*)` | Populates LAN IPv4/MAC where available; online identity fields remain zero. Returned flags are provisional. |
| `XSocketCreate` | `SOCKET WINAPI (int af, int type, int protocol)` | Opaque 32-bit token; `INVALID_SOCKET` on unsupported family/type or failure. TCP and UDP/IPv4 only. |
| `XSocketClose` | `int WINAPI (SOCKET)` | 0 on registry removal; `SOCKET_ERROR` for invalid token. In-flight operations retain a native socket lease until completion. |
| `XSocketBind` | `int WINAPI (SOCKET, const sockaddr*, int)` | Winsock-style 0 / `SOCKET_ERROR`; IPv4 only. |
| `XSocketConnect` | `int WINAPI (SOCKET, const sockaddr*, int)` | Winsock-style 0 / `SOCKET_ERROR`; IPv4 only. |
| `XSocketListen` | `int WINAPI (SOCKET, int backlog)` | Winsock-style result. |
| `XSocketAccept` | `SOCKET WINAPI (SOCKET, sockaddr*, int*)` | New opaque token or `INVALID_SOCKET`. |
| `XSocketShutdown` | `int WINAPI (SOCKET, int how)` | Winsock-style result. |
| `XSocketIOCTLSocket` | `int WINAPI (SOCKET, long, u_long*)` | Currently supports `FIONBIO` only. |
| `XSocketSetSockOpt` | `int WINAPI (SOCKET, int, int, const char*, int)` | Forwards supported options to host Winsock. |
| `XSocketGetSockOpt` | `int WINAPI (SOCKET, int, int, char*, int*)` | Forwards supported options to host Winsock. |
| `XSocketGetSockName` | `int WINAPI (SOCKET, sockaddr*, int*)` | Local endpoint query. |
| `XSocketGetPeerName` | `int WINAPI (SOCKET, sockaddr*, int*)` | Peer endpoint query. |
| `XSocketSend` | `int WINAPI (SOCKET, const char*, int, int flags)` | Bytes sent or `SOCKET_ERROR`. |
| `XSocketRecv` | `int WINAPI (SOCKET, char*, int, int flags)` | Bytes received or `SOCKET_ERROR`. |
| `XSocketSendTo` | `int WINAPI (SOCKET, const char*, int, int, const sockaddr*, int)` | Bytes sent or `SOCKET_ERROR`; IPv4 destination only. |
| `XSocketRecvFrom` | `int WINAPI (SOCKET, char*, int, int, sockaddr*, int*)` | Bytes received or `SOCKET_ERROR`; IPv4 source endpoint when output buffer is supplied. |
| `XSocketHTONL` / `XSocketNTOHL` | `u_long WINAPI (u_long)` | Host/network 32-bit byte-order conversion. |
| `XSocketHTONS` / `XSocketNTOHS` | `u_short WINAPI (u_short)` | Host/network 16-bit byte-order conversion. |

## Structure inventory

- `XNetStartupParams`: 13 one-byte configuration fields; current static size assertion 13.
- `XNADDR`: `IN_ADDR ina`, `IN_ADDR inaOnline`, `WORD wPortOnline`, `BYTE abEnet[6]`, `BYTE abOnline[20]`; current size assertion 36.
- `XNDNS`: `INT iStatus`, `UINT cina`, `IN_ADDR aina[8]`; current size assertion 40.
- Uses Windows `IN_ADDR`, `sockaddr`, `sockaddr_in`, `WSAEVENT`, and Winsock scalar types from the guest Win32 SDK headers.

## Internal transport bridge (not title ABI)

The following `__cdecl` functions are implementation-private guest32 helpers. They use fixed-width 32-bit tokens and values; the kernel should not register these as Xbox imports:
`XboxNetStartup`, `XboxNetCleanup`, `XboxNetResolveIPv4`, `XboxNetSocketCreate`, `XboxNetSocketConnect`, `XboxNetSocketConnectAddress`, `XboxNetSocketBindAddress`, `XboxNetSocketShutdown`, `XboxNetSocketGetName`, `XboxNetSocketGetPeerName`, `XboxNetSocketSetOption`, `XboxNetSocketGetOption`, `XboxNetSocketBind`, `XboxNetSocketListen`, `XboxNetSocketAccept`, `XboxNetSocketSend`, `XboxNetSocketReceive`, `XboxNetSocketSendTo`, `XboxNetSocketReceiveFrom`, `XboxNetSocketSetNonBlocking`, `XboxNetSocketSetTimeout`, `XboxNetSocketClose`.

## Explicitly not implemented / not claimed

- XNet QoS, key registration, security associations, NAT traversal and online identity negotiation.
- XNet/XOnline sign-in/session services.
- VDP voice protocol (protocol 254 is rejected rather than emulated as ordinary UDP).
- Full system-link/broadcast semantics, overlapped I/O and select/event integration.
- Complete WSA error-state compatibility and full Xbox-specific socket option translation.
- Verified title-call interception for statically linked XNet code.
- Real Insignia sign-in or supported-title end-to-end connectivity.

## Validation still required

1. Compare every declaration, structure packing/size, constant and return convention against an authoritative Xbox-compatible SDK header.
2. Build the x86 guest and `network_smoke` with MSVC; run the smoke test and record exact output.
3. Add per-function ABI tests, including invalid pointers/lengths, event semantics, socket options, nonblocking I/O and close-vs-operation races.
4. Agree with kernel/loader on a guest API registration format and prove one verified call path from a real XBE.
5. Design static XNet interception separately; do not patch guessed signatures or claim that these compiled functions automatically redirect retail calls.
