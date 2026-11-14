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
- Structures use `#pragma pack(push, 4)` in the current header. The static assertions currently enforce `XNetStartupParams=12`, `XNADDR=36`, `XNDNS=40` bytes. These sizes still require comparison against an authoritative Xbox SDK-compatible header before claiming retail ABI compatibility.

## Implemented title-facing functions

| Function | Current declaration / arguments | Return and current behaviour |
|---|---|---|
| `XNetStartup` | `int WINAPI (const XNetStartupParams*)` | 0 on startup success; otherwise Winsock error. Reference-counted Winsock 2.2 startup. |
| `XNetCleanup` | `int WINAPI ()` | 0; decrements startup reference count and cleans up transport at zero. |
| `XNetDnsLookup` | `int WINAPI (const char*, WSAEVENT, XNDNS**)` | Queues a worker-thread lookup, returns an owned `XNDNS` result, then signals the supplied event when `iStatus`/`cina` are ready. Caller must wait for completion before reading or releasing the result. |
| `XNetDnsRelease` | `int WINAPI (XNDNS*)` | 0; frees result allocated by this implementation. |
| `XNetRandom` | `int WINAPI (BYTE*, UINT)` | Uses Windows system-preferred BCrypt RNG; 0 on success, WSA error on failure. |
| `XNetInAddrToString` | `int WINAPI (IN_ADDR, char*, int)` | Uses `InetNtopA` to write a thread-safe IPv4 string into the caller buffer; returns a Winsock error on invalid/short output buffer. |
| `XNetXnAddrToInAddr` | `int WINAPI (const XNADDR*, const XNKID*, IN_ADDR*)` | Returns `WSAEOPNOTSUPP` until online key/address registration is implemented; never fabricates a mapping. |
| `XNetServerToInAddr` | `int WINAPI (IN_ADDR, DWORD, IN_ADDR*)` | Returns `WSAEOPNOTSUPP` until service-ID resolution is implemented. |
| `XNetInAddrToXnAddr` | `int WINAPI (const IN_ADDR, XNADDR*, XNKID*)` | Returns `WSAEOPNOTSUPP` until registered online address/key state exists. |
| `XNetUnregisterInAddr` | `int WINAPI (const IN_ADDR)` | Returns `WSAEOPNOTSUPP`; no registration table exists yet. |
| `XNetGetDebugXnAddr` | `DWORD WINAPI (XNADDR*)` | Returns no debug address (zero); devkit-only identity is not synthesized. |
| `XNetGetEthernetLinkStatus` | `DWORD WINAPI ()` | Host adapter-derived flags using the Xbox bit definitions: ACTIVE, 100MBPS/10MBPS and WIRELESS. Duplex is left clear because IP Helper does not expose it in this adapter record. |
| `XNetGetTitleXnAddr` | `DWORD WINAPI (XNADDR*)` | Requires XNet startup; returns `XNET_GET_XNADDR_NONE` when unavailable. Populates LAN IPv4/MAC; reports verified DHCP/static, gateway and DNS flags. Online identity fields remain zero. |
| `XSocketCreate` | `SOCKET WINAPI (int af, int type, int protocol)` | Opaque 32-bit token; `INVALID_SOCKET` on unsupported family/type or failure. TCP and UDP/IPv4 only. |
| `XSocketClose` | `int WINAPI (SOCKET)` | 0 on registry removal; `SOCKET_ERROR` for invalid token. In-flight operations retain a native socket lease until completion. |
| `XSocketBind` | `int WINAPI (SOCKET, const sockaddr*, int)` | Winsock-style 0 / `SOCKET_ERROR`; IPv4 only. |
| `XSocketConnect` | `int WINAPI (SOCKET, const sockaddr*, int)` | Winsock-style 0 / `SOCKET_ERROR`; IPv4 only. |
| `XSocketListen` | `int WINAPI (SOCKET, int backlog)` | Winsock-style result. |
| `XSocketAccept` | `SOCKET WINAPI (SOCKET, sockaddr*, int*)` | New opaque token or `INVALID_SOCKET`; validates the optional address/length pair. |
| `XSocketShutdown` | `int WINAPI (SOCKET, int how)` | Winsock-style result. |
| `XSocketIOCTLSocket` | `int WINAPI (SOCKET, long, u_long*)` | Currently supports `FIONBIO` only. |
| `XSocketSetSockOpt` | `int WINAPI (SOCKET, int, int, const char*, int)` | Forwards supported options to host Winsock. |
| `XSocketGetSockOpt` | `int WINAPI (SOCKET, int, int, char*, int*)` | Forwards supported options to host Winsock. |
| `XSocketGetSockName` | `int WINAPI (SOCKET, sockaddr*, int*)` | Local endpoint query. |
| `XSocketGetPeerName` | `int WINAPI (SOCKET, sockaddr*, int*)` | Peer endpoint query. |
| `XSocketSend` | `int WINAPI (SOCKET, const char*, int, int flags)` | Bytes sent or `SOCKET_ERROR`. |
| `XSocketRecv` | `int WINAPI (SOCKET, char*, int, int flags)` | Bytes received or `SOCKET_ERROR`. |
| `XSocketSendTo` | `int WINAPI (SOCKET, const char*, int, int, const sockaddr*, int)` | Bytes sent or `SOCKET_ERROR`; IPv4 destination only. |
| `XSocketRecvFrom` | `int WINAPI (SOCKET, char*, int, int, sockaddr*, int*)` | Bytes received or `SOCKET_ERROR`; validates source-address/length arguments and writes the IPv4 source endpoint when requested. |
| `XSocketHTONL` / `XSocketNTOHL` | `u_long WINAPI (u_long)` | Host/network 32-bit byte-order conversion. |
| `XSocketHTONS` / `XSocketNTOHS` | `u_short WINAPI (u_short)` | Host/network 16-bit byte-order conversion. |

## Verified status flags

The header defines the Xbox status values used by the implementation:
- `XNET_GET_XNADDR_NONE=0x1`, `ETHERNET=0x2`, `STATIC=0x4`, `DHCP=0x8`, `GATEWAY=0x20`, `DNS=0x40`, `ONLINE=0x80`, `TROUBLESHOOT=0x8000`.
- `XNET_ETHERNET_LINK_ACTIVE=0x1`, `100MBPS=0x2`, `10MBPS=0x4`, `FULL_DUPLEX=0x8`, `HALF_DUPLEX=0x10`, `WIRELESS=0x20`.

The host adapter query reports only properties it can observe. Duplex flags are intentionally not set because the Windows IP Helper adapter record does not expose duplex mode.

## Structure inventory

- `XNetStartupParams`: 13 one-byte configuration fields; current static size assertion 13.
- `XNADDR`: `IN_ADDR ina`, `IN_ADDR inaOnline`, `WORD wPortOnline`, `BYTE abEnet[6]`, `BYTE abOnline[20]`; current size assertion 36.
- `XNDNS`: `INT iStatus`, `UINT cina`, `IN_ADDR aina[8]`; current size assertion 40.
- Uses Windows `IN_ADDR`, `sockaddr`, `sockaddr_in`, `WSAEVENT`, and Winsock scalar types from the guest Win32 SDK headers.

## WSA helper aliases

The Xbox socket API list also includes Winsock helpers such as event operations, last-error access and `inet_addr`. To avoid colliding with the host process's imported `ws2_32` symbols, the current guest target implements prefixed aliases that the future guest API registry can map by original symbol identity:
- `WSAStartup` → `XboxWSAStartup` (shares the guest transport Winsock reference count)
- `WSACleanup` → `XboxWSACleanup`
- `WSACreateEvent` → `XboxWSACreateEvent`
- `WSACloseEvent` → `XboxWSACloseEvent`
- `WSASetEvent` → `XboxWSASetEvent`
- `WSAResetEvent` → `XboxWSAResetEvent`
- `WSAWaitForMultipleEvents` → `XboxWSAWaitForMultipleEvents`
- `WSAGetLastError` → `XboxWSAGetLastError`
- `WSASetLastError` → `XboxWSASetLastError`
- `inet_addr` → `XboxWSAInetAddr`

These wrappers use the guest process's own Winsock startup, event and error facilities.

### Original Xbox socket symbol → guest implementation map

All targets below use `WINAPI` (stdcall on x86), and all socket handles are opaque 32-bit tokens. The registry must bind the exact Xbox import identity to the listed guest function address; the internal `XSocket*` names are not claimed to be original Xbox export names.

| Xbox socket symbol | Guest implementation |
|---|---|
| `socket` | `XSocketCreate` |
| `closesocket` | `XSocketClose` |
| `bind` | `XSocketBind` |
| `connect` | `XSocketConnect` |
| `listen` | `XSocketListen` |
| `accept` | `XSocketAccept` |
| `shutdown` | `XSocketShutdown` |
| `ioctlsocket` | `XSocketIOCTLSocket` |
| `select` | `XSocketSelect` | Translates guest 32-bit socket tokens to native host sockets, invokes Winsock `select`, then restores ready sets using guest tokens. `nfds` is ignored per Winsock semantics. |
| `setsockopt` / `getsockopt` | `XSocketSetSockOpt` / `XSocketGetSockOpt` |
| `getsockname` / `getpeername` | `XSocketGetSockName` / `XSocketGetPeerName` |
| `send` / `recv` | `XSocketSend` / `XSocketRecv` |
| `sendto` / `recvfrom` | `XSocketSendTo` / `XSocketRecvFrom` |
| `htonl` / `ntohl` | `XSocketHTONL` / `XSocketNTOHL` |
| `htons` / `ntohs` | `XSocketHTONS` / `XSocketNTOHS` |

`WSARecv`, `WSARecvFrom`, `WSASend`, `WSASendTo`, `WSAGetOverlappedResult` and `WSACancelOverlappedIO` are not implemented. Do not register them until their guest-pointer, event, completion and cancellation semantics are implemented and tested. The full socket API names in the Xbox `SOCKAPILIST` are the standard `socket`, `closesocket`, `bind`, `connect`, `listen`, `accept`, `select`, `send`, `recv`, etc.; current `XSocket*` wrappers are an internal compatibility surface and still need explicit name-to-address registration. Overlapped I/O (`WSARecv`/`WSASend`) and several helpers remain unsupported.

## Internal transport bridge (not title ABI)

The following `__cdecl` functions are implementation-private guest32 helpers. They use fixed-width 32-bit tokens and values; the kernel should not register these as Xbox imports:
`XboxNetStartup`, `XboxNetCleanup`, `XboxNetResolveIPv4`, `XboxNetSocketCreate`, `XboxNetSocketConnect`, `XboxNetSocketConnectAddress`, `XboxNetSocketBindAddress`, `XboxNetSocketShutdown`, `XboxNetSocketGetName`, `XboxNetSocketGetPeerName`, `XboxNetSocketSetOption`, `XboxNetSocketGetOption`, `XboxNetSocketBind`, `XboxNetSocketListen`, `XboxNetSocketAccept`, `XboxNetSocketSend`, `XboxNetSocketReceive`, `XboxNetSocketSendTo`, `XboxNetSocketReceiveFrom`, `XboxNetSocketSetNonBlocking`, `XboxNetSocketSetTimeout`, `XboxNetSocketClose`.

## Explicitly not implemented / not claimed

- XNet QoS, key creation/registration, security associations, NAT traversal and online identity negotiation.
- XNet/XOnline sign-in/session services.
- VDP voice protocol (protocol 254 is rejected rather than emulated as ordinary UDP).
- Full system-link/broadcast semantics and overlapped I/O.
- Complete WSA error-state compatibility and full Xbox-specific socket option translation.
- Verified title-call interception for statically linked XNet code.
- Real Insignia sign-in or supported-title end-to-end connectivity.

## Validation still required

1. Compare every declaration, structure packing/size, constant and return convention against an authoritative Xbox-compatible SDK header.
2. Build the x86 guest and `network_smoke` with MSVC; run the smoke test and record exact output.
3. Add per-function ABI tests, including invalid pointers/lengths, asynchronous DNS event ordering and cleanup races, socket options, nonblocking I/O, select edge cases, overlapped behavior and close-vs-operation races.
4. Agree with kernel/loader on a guest API registration format and prove one verified call path from a real XBE.
5. Design static XNet interception separately; do not patch guessed signatures or claim that these compiled functions automatically redirect retail calls.


## ABI correction and key-registration tranche (2026-11-14)

The XDK-compatible RXDK-SDK `include/winsockx.h` reference shows the retail
`XNetStartupParams` is **12 bytes**, not 13. Its fields are:
`cfgSizeOfStruct`, `cfgFlags`, `cfgPrivatePoolSizeInPages`,
`cfgEnetReceiveQueueLength`, `cfgIpFragMaxSimultaneous`,
`cfgIpFragMaxPacketDiv256`, `cfgSockMaxSockets`,
`cfgSockDefaultRecvBufsizeInK`, `cfgSockDefaultSendBufsizeInK`,
`cfgKeyRegMax`, `cfgSecRegMax`, and `cfgQosDataLimitDiv4`.
The prior local declaration had the wrong field set and size; it has been replaced
and compile-time offset/size assertions now enforce the reference layout.

The same reference defines `XNKEY` as 16 bytes and uses one-byte packing for
the XNet structures. The guest header now models `XNKEY`, `XNQOSINFO`, and
`XNQOS`, with x86 size/offset assertions.

The key API tranche now implements random system-link key creation and a
bounded registration table with duplicate detection, configured capacity, and
secure wiping on unregister/cleanup. **This is not the original XNet secure
packet protocol:** Diffie-Hellman exchange, HMAC/DES packet protection,
security associations, NAT traversal, QoS, and XBE call dispatch remain
unimplemented and are not claimed.

Reference: [RXDK-SDK winsockx.h](https://github.com/Team-Resurgent/RXDK-SDK/blob/main/include/winsockx.h);
implementation reference: [RXDK-Libs ip.cpp](https://github.com/Team-Resurgent/RXDK-Libs/blob/main/libs/libxnet/net/ip.cpp).


The invalid `XNetStartupParams.cfgSizeOfStruct` error is `WSAEFAULT`, matching
the original XNet entry-point parameter check (rather than the generic
`WSAEINVAL` used by the first smoke-test draft). The smoke test asserts this
exact error result.
