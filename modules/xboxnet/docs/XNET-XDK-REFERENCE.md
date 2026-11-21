# Original Xbox XNet / XOnline reference notes

These notes separate the guest networking API from Xbox online account services.
They are implementation guidance, not a claim of full Xbox Live compatibility.

## Useful public implementation reference

- [Team Resurgent RXDK-Libs: `libs/libxnet/net/xnet.cpp`](https://github.com/Team-Resurgent/RXDK-Libs/blob/b7c9f2b3a24ece1aec850dca52155a4f8633686c/libs/libxnet/net/xnet.cpp)
- [RXDK-SDK](https://github.com/Team-Resurgent/RXDK-SDK)

RXDK-SDK describes itself as a from-source, MSVC-free consumer port of the
original-Xbox runtime and lists `libxnet.lib` as the XNet TCP/IP stack.
The RXDK-Libs XNet source exposes the public API entry points and shows how
those calls are delegated to the underlying stack. The file itself identifies
Microsoft-authored portions and separately licensed Team Resurgent changes;
XboxRuntime should use it as a technical reference and must preserve applicable
license/provenance requirements before copying any code.

## XNet API surface to map in XboxRuntime

The public XNet surface includes:

- Startup/lifetime: `XNetStartup`, `XNetCleanup`.
- DNS: `XNetDnsLookup`, `XNetDnsRelease`.
- Local address/status: `XNetGetTitleXnAddr`, `XNetGetEthernetLinkStatus`.
- Secure address/key mapping: `XNetCreateKey`, `XNetRegisterKey`,
  `XNetUnregisterKey`, `XNetXnAddrToInAddr`, `XNetInAddrToXnAddr`,
  `XNetUnregisterInAddr`.
- QoS and peer address helpers: `XNetQosListen`, `XNetQosXnAddr`,
  `XNetQosServer`, `XNetQosRelease`, plus connect/status helpers.

The RXDK implementation demonstrates the API boundaries and error/initialization
checks. It is a better reference for original-Xbox XNet than an Xbox 360
Games-for-Windows-Live ordinal list.

## XNet is not XOnline

XNet is the guest networking stack: sockets, DNS, secure peer addressing, and
QoS. Account/profile operations are in the separate XOnline/XUser family.
Original-Xbox titles use the `XOnline` API family (for example
`XOnlineStartup`, `XOnlineCleanup`, `XOnlineGetUsers`, `XOnlineLogon`, and
the asynchronous task-pump APIs). Do not assume the Xbox 360/GFWL
`XUserReadProfileSettings` and `XUserWriteProfileSettings` exports are the
right original-Xbox interfaces. XboxNet's SQLite profile service is currently
an internal backend model; it is **not** yet the original XOnline account
storage, logon, or service protocol.

## Avoid mixing platforms

- [NeKzor/xdead](https://github.com/NeKzor/xdead) is a Games for Windows Live
  (GFWL / Xbox 360-era XLive) replacement project. Its ordinal list is not the
  original Xbox XDK XNet/XOnline ABI.
- [XboxDevWiki System Link](https://xboxdevwiki.net/System_Link) documents
  original-Xbox secure system-link traffic and a small XNet API list. Treat its
  protocol details as research notes and validate them against source and
  captures before implementing compatibility.

## Next compatibility steps

1. Map the exact original-Xbox XNet and XOnline exports used by the target XBE.
2. Record signatures, structure sizes, constants, calling conventions, and
   return semantics from public headers/source or independently verified
   binaries.
3. Implement guest API compatibility separately from XboxNet's server services.
4. Add captured-console integration tests before claiming a title can sign in.
