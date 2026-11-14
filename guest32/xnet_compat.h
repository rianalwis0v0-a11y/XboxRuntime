#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <cstdint>
#include <cstddef>

// Original Xbox XNet address/link status flags.
constexpr DWORD XNET_GET_XNADDR_NONE = 0x00000001u;
constexpr DWORD XNET_GET_XNADDR_ETHERNET = 0x00000002u;
constexpr DWORD XNET_GET_XNADDR_STATIC = 0x00000004u;
constexpr DWORD XNET_GET_XNADDR_DHCP = 0x00000008u;
constexpr DWORD XNET_GET_XNADDR_GATEWAY = 0x00000020u;
constexpr DWORD XNET_GET_XNADDR_DNS = 0x00000040u;
constexpr DWORD XNET_GET_XNADDR_ONLINE = 0x00000080u;
constexpr DWORD XNET_GET_XNADDR_TROUBLESHOOT = 0x00008000u;
constexpr DWORD XNET_ETHERNET_LINK_ACTIVE = 0x00000001u;
constexpr DWORD XNET_ETHERNET_LINK_100MBPS = 0x00000002u;
constexpr DWORD XNET_ETHERNET_LINK_10MBPS = 0x00000004u;
constexpr DWORD XNET_ETHERNET_LINK_FULL_DUPLEX = 0x00000008u;
constexpr DWORD XNET_ETHERNET_LINK_HALF_DUPLEX = 0x00000010u;
constexpr DWORD XNET_ETHERNET_LINK_WIRELESS = 0x00000020u;

// Original Xbox XNet ABI structures. Keep these fixed-width: this file is
// compiled only into the dedicated 32-bit guest process.
#pragma pack(push, 1)
// XDK-compatible winsockx.h uses one-byte packing for the public XNet ABI.
struct XNetStartupParams {
    BYTE cfgSizeOfStruct;
    BYTE cfgFlags;
    BYTE cfgPrivatePoolSizeInPages;
    BYTE cfgEnetReceiveQueueLength;
    BYTE cfgIpFragMaxSimultaneous;
    BYTE cfgIpFragMaxPacketDiv256;
    BYTE cfgSockMaxSockets;
    BYTE cfgSockDefaultRecvBufsizeInK;
    BYTE cfgSockDefaultSendBufsizeInK;
    BYTE cfgKeyRegMax;
    BYTE cfgSecRegMax;
    BYTE cfgQosDataLimitDiv4;
};
struct XNKID { BYTE ab[8]; };
struct XNKEY { BYTE ab[16]; };
struct XNADDR {
    IN_ADDR ina;
    IN_ADDR inaOnline;
    WORD wPortOnline;
    BYTE abEnet[6];
    BYTE abOnline[20];
};
struct XNDNS {
    INT iStatus;
    UINT cina;
    IN_ADDR aina[8];
};
struct XNQOSINFO {
    BYTE bFlags;
    BYTE bReserved;
    WORD cProbesXmit;
    WORD cProbesRecv;
    WORD cbData;
    BYTE* pbData;
    WORD wRttMinInMsecs;
    WORD wRttMedInMsecs;
    DWORD dwUpBitsPerSec;
    DWORD dwDnBitsPerSec;
};
struct XNQOS {
    UINT cxnqos;
    UINT cxnqosPending;
    XNQOSINFO axnqosinfo[1];
};
#pragma pack(pop)

// NXDK's xboxkrnl/xboxdef.h defines the original-Xbox scalar ABI:
// BYTE/UCHAR=8-bit, WORD/USHORT=16-bit, and DWORD/ULONG/UINT=32-bit.
// Keep this host-side compatibility header honest about those guest widths.
// NXDK does not currently ship an XNet/XSocket declaration header; the
// XNet-specific structures below therefore remain separately audited.
static_assert(sizeof(BYTE) == 1, "Xbox BYTE ABI width (NXDK xboxdef.h)");
static_assert(sizeof(WORD) == 2, "Xbox WORD ABI width (NXDK xboxdef.h)");
static_assert(sizeof(UINT) == 4, "Xbox UINT ABI width (NXDK xboxdef.h)");
static_assert(sizeof(DWORD) == 4, "Xbox DWORD ABI width (NXDK xboxdef.h)");
static_assert(sizeof(ULONG) == 4, "Xbox ULONG ABI width (NXDK xboxdef.h)");

static_assert(sizeof(XNetStartupParams) == 12, "XNetStartupParams ABI size (XDK winsockx.h)");
static_assert(offsetof(XNetStartupParams, cfgPrivatePoolSizeInPages) == 2, "XNetStartupParams field offset");
static_assert(offsetof(XNetStartupParams, cfgSockMaxSockets) == 6, "XNetStartupParams field offset");
static_assert(offsetof(XNetStartupParams, cfgQosDataLimitDiv4) == 11, "XNetStartupParams field offset");
static_assert(sizeof(XNKID) == 8, "XNKID ABI size");
static_assert(sizeof(XNKEY) == 16, "XNKEY ABI size");
static_assert(sizeof(XNADDR) == 36, "XNADDR ABI size");
static_assert(sizeof(XNDNS) == 40, "XNDNS ABI size");
static_assert(sizeof(XNQOSINFO) == 24, "XNQOSINFO x86 ABI size");
static_assert(offsetof(XNQOSINFO, pbData) == 8, "XNQOSINFO guest pointer offset");
static_assert(sizeof(XNQOS) == 32, "XNQOS first-result ABI size");

extern "C" {
int WINAPI XNetStartup(const XNetStartupParams* params);
int WINAPI XNetCleanup();
int WINAPI XNetCreateKey(XNKID* keyId, XNKEY* key);
int WINAPI XNetRegisterKey(const XNKID* keyId, const XNKEY* key);
int WINAPI XNetUnregisterKey(const XNKID* keyId);
int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** result);
int WINAPI XNetDnsRelease(XNDNS* result);
int WINAPI XNetRandom(BYTE* buffer, UINT length);
int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity);
int WINAPI XNetXnAddrToInAddr(const XNADDR* address, const XNKID* keyId, IN_ADDR* output);
int WINAPI XNetServerToInAddr(IN_ADDR address, DWORD serviceId, IN_ADDR* output);
int WINAPI XNetInAddrToXnAddr(const IN_ADDR address, XNADDR* output, XNKID* keyId);
int WINAPI XNetUnregisterInAddr(const IN_ADDR address);
DWORD WINAPI XNetGetEthernetLinkStatus();
DWORD WINAPI XNetGetTitleXnAddr(XNADDR* address);
DWORD WINAPI XNetGetDebugXnAddr(XNADDR* address);
}

extern "C" {
SOCKET WINAPI XSocketCreate(int af, int type, int protocol);
int WINAPI XSocketClose(SOCKET socket);
int WINAPI XSocketBind(SOCKET socket, const struct sockaddr* address, int addressLength);
int WINAPI XSocketConnect(SOCKET socket, const struct sockaddr* address, int addressLength);
int WINAPI XSocketListen(SOCKET socket, int backlog);
SOCKET WINAPI XSocketAccept(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketShutdown(SOCKET socket, int how);
int WINAPI XSocketIOCTLSocket(SOCKET socket, long command, u_long* argument);
int WINAPI XSocketSelect(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout);
int WINAPI XSocketSetSockOpt(SOCKET socket, int level, int option, const char* value, int length);
int WINAPI XSocketGetSockOpt(SOCKET socket, int level, int option, char* value, int* length);
int WINAPI XSocketGetSockName(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketGetPeerName(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketSend(SOCKET socket, const char* buffer, int length, int flags);
int WINAPI XSocketRecv(SOCKET socket, char* buffer, int length, int flags);
int WINAPI XSocketSendTo(SOCKET socket, const char* buffer, int length, int flags, const struct sockaddr* to, int toLength);
int WINAPI XSocketRecvFrom(SOCKET socket, char* buffer, int length, int flags, struct sockaddr* from, int* fromLength);
u_long WINAPI XSocketHTONL(u_long value);
u_long WINAPI XSocketNTOHL(u_long value);
u_short WINAPI XSocketHTONS(u_short value);
u_short WINAPI XSocketNTOHS(u_short value);
}

// Prefixed guest aliases for the Xbox SOCKAPILIST WSA helpers. The guest API
// registry maps the original symbol names to these addresses, avoiding symbol
// collisions with the host process's ws2_32 imports.
extern "C" {
int WINAPI XboxWSAStartup(WORD versionRequested, LPWSADATA data);
int WINAPI XboxWSACleanup();
WSAEVENT WINAPI XboxWSACreateEvent();
BOOL WINAPI XboxWSACloseEvent(WSAEVENT eventHandle);
BOOL WINAPI XboxWSASetEvent(WSAEVENT eventHandle);
BOOL WINAPI XboxWSAResetEvent(WSAEVENT eventHandle);
DWORD WINAPI XboxWSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD timeout, BOOL alertable);
int WINAPI XboxWSAGetLastError();
void WINAPI XboxWSASetLastError(int error);
ULONG WINAPI XboxWSAInetAddr(const char* address);
}
