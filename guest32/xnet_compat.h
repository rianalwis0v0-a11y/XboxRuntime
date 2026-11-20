#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <cstdint>

// Original Xbox XNet ABI structures. Keep these fixed-width: this file is
// compiled only into the dedicated 32-bit guest process.
#pragma pack(push, 4)
struct XNetStartupParams {
    BYTE cfgSizeOfStruct;
    BYTE cfgFlags;
    BYTE cfgSockMaxDgramSockets;
    BYTE cfgSockMaxStreamSockets;
    BYTE cfgSockDefaultRecvBufsizeInK;
    BYTE cfgSockDefaultSendBufsizeInK;
    BYTE cfgKeyRegMax;
    BYTE cfgSecRegMax;
    BYTE cfgQosDataLimitDiv4;
    BYTE cfgQosProbeTimeoutInSeconds;
    BYTE cfgQosProbeRetries;
    BYTE cfgQosSrvMaxSimultaneousResponses;
    BYTE cfgQosPairWaitTimeInSeconds;
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
using TSADDR = XNADDR;
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
    DWORD cxnqos;
    DWORD cxnqosPending;
    XNQOSINFO axnqosinfo[1];
};
struct XNQOSLISTENSTATS {
    DWORD dwSizeOfStruct;
    DWORD dwRequestsReceived;
    DWORD dwProbesReceived;
    DWORD dwSlotsFullDiscards;
    DWORD dwDataRepliesSent;
    DWORD dwDataReplyBytesSent;
    DWORD dwProbeRepliesSent;
};
#pragma pack(pop)

static_assert(sizeof(XNetStartupParams) == 13, "XNetStartupParams ABI size");
static_assert(sizeof(XNKID) == 8, "XNKID ABI size");
static_assert(sizeof(XNKEY) == 16, "XNKEY ABI size");
static_assert(sizeof(XNADDR) == 36, "XNADDR ABI size");
static_assert(sizeof(XNDNS) == 40, "XNDNS ABI size");
static_assert(sizeof(XNQOSINFO) == 24, "XNQOSINFO ABI size");
static_assert(sizeof(XNQOS) == 32, "XNQOS ABI size");
static_assert(sizeof(XNQOSLISTENSTATS) == 28, "XNQOSLISTENSTATS ABI size");

#define XNET_QOS_LISTEN_ENABLE 0x00000001u
#define XNET_QOS_LISTEN_DISABLE 0x00000002u
#define XNET_QOS_LISTEN_SET_DATA 0x00000004u
#define XNET_QOS_LISTEN_SET_BITSPERSEC 0x00000008u
#define XNET_QOS_LISTEN_RELEASE 0x00000010u
#define XNET_XNQOSINFO_COMPLETE 0x01u
#define XNET_XNQOSINFO_TARGET_CONTACTED 0x02u
#define XNET_XNQOSINFO_TARGET_DISABLED 0x04u
#define XNET_XNQOSINFO_DATA_RECEIVED 0x08u
#define XNET_XNQOSINFO_PARTIAL_COMPLETE 0x10u

#define XNET_GET_XNADDR_NONE 0x00000001u
#define XNET_GET_XNADDR_ETHERNET 0x00000002u
#define XNET_GET_XNADDR_STATIC 0x00000004u
#define XNET_GET_XNADDR_DHCP 0x00000008u
#define XNET_GET_XNADDR_GATEWAY 0x00000020u
#define XNET_GET_XNADDR_DNS 0x00000040u
#define XNET_XNADDR_PLATFORM_XBOX1 0x00000000u
#define XNET_XNADDR_PLATFORM_XBOX360 0x00000001u
#define XNET_XNADDR_PLATFORM_WINPC 0x00000002u

extern "C" {
int WINAPI XWSAStartup(WORD requestedVersion, LPWSADATA output);
int WINAPI XWSACleanup();
int WINAPI XSocketWSAGetLastError();
void WINAPI XWSASetLastError(int error);
WSAEVENT WINAPI XWSACreateEvent();
BOOL WINAPI XWSACloseEvent(WSAEVENT eventHandle);
BOOL WINAPI XWSASetEvent(WSAEVENT eventHandle);
BOOL WINAPI XWSAResetEvent(WSAEVENT eventHandle);
DWORD WINAPI XWSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events,
    BOOL waitAll, DWORD timeout, BOOL alertable);
BOOL WINAPI XWSAFDIsSet(SOCKET socket, fd_set* set);
int WINAPI XWSAEventSelect(SOCKET socket, WSAEVENT eventHandle, long networkEvents);
WSAEVENT WINAPI XSocketWSACreateEvent();
BOOL WINAPI XSocketWSACloseEvent(WSAEVENT eventHandle);
BOOL WINAPI XSocketWSASetEvent(WSAEVENT eventHandle);
BOOL WINAPI XSocketWSAResetEvent(WSAEVENT eventHandle);
DWORD WINAPI XSocketWSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD timeout, BOOL alertable);
BOOL WINAPI XSocketWSAFDIsSet(SOCKET socket, fd_set* set);
int WINAPI XSocketWSAEventSelect(SOCKET socket, WSAEVENT eventHandle, long networkEvents);
void WINAPI XSocketWSASetLastError(int error);

int WINAPI XNetStartup(const XNetStartupParams* params);
int WINAPI XNetCleanup();
int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** result);
int WINAPI XNetDnsRelease(XNDNS* result);
int WINAPI XNetQosListen(const XNKID* keyId, const BYTE* data, UINT dataSize,
    DWORD bitsPerSecond, DWORD flags);
int WINAPI XNetQosLookup(UINT countXnAddr, const XNADDR* xnAddrs[],
    const XNKID* keyIds[], const XNKEY* keys[], UINT countInAddr,
    const IN_ADDR inAddrs[], const DWORD serviceIds[], UINT probes,
    DWORD bitsPerSecond, DWORD flags, WSAEVENT eventHandle, XNQOS** result);
int WINAPI XNetQosServiceLookup(DWORD flags, WSAEVENT eventHandle, XNQOS** result);
int WINAPI XNetQosRelease(XNQOS* result);
int WINAPI XNetQosGetListenStats(const XNKID* keyId, XNQOSLISTENSTATS* stats);
int WINAPI XNetRandom(BYTE* buffer, UINT length);
int WINAPI XNetCreateKey(XNKID* keyId, XNKEY* key);
int WINAPI XNetRegisterKey(const XNKID* keyId, const XNKEY* key);
int WINAPI XNetUnregisterKey(const XNKID* keyId);
int WINAPI XNetXnAddrToInAddr(const XNADDR* address, const XNKID* keyId, IN_ADDR* output);
int WINAPI XNetInAddrToXnAddr(IN_ADDR address, XNADDR* output, XNKID* keyId);
int WINAPI XNetUnregisterInAddr(IN_ADDR address);
int WINAPI XNetTsAddrToInAddr(const TSADDR* address, DWORD serviceId, const XNKID* keyId, IN_ADDR* output);
int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity);
int WINAPI XNetServerToInAddr(IN_ADDR address, DWORD serviceId, IN_ADDR* output);
int WINAPI XNetInAddrToServer(IN_ADDR address, IN_ADDR* output);
DWORD WINAPI XNetGetEthernetLinkStatus();
DWORD WINAPI XNetGetTitleXnAddr(XNADDR* address);
DWORD WINAPI XNetGetDebugXnAddr(XNADDR* address);
int WINAPI XNetGetXnAddrPlatform(const XNADDR* address, DWORD* platform);
DWORD WINAPI XNetGetBroadcastVersionStatus(BOOL reset);
int WINAPI XNetGetSystemLinkPort(WORD* port);
int WINAPI XNetSetSystemLinkPort(WORD port);
}

extern "C" {
SOCKET WINAPI XSocketCreate(int af, int type, int protocol);
SOCKET WINAPI XCreateSocket(int af, int type, int protocol);
BOOL WINAPI XSocketWSAGetOverlappedResult(SOCKET socket, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait, LPDWORD flags);
int WINAPI XSocketWSACancelOverlappedIO(SOCKET socket);
int WINAPI XSocketWSARecv(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD bytes, LPDWORD flags, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int WINAPI XSocketWSARecvFrom(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD bytes, LPDWORD flags, sockaddr* from, LPINT fromLength, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int WINAPI XSocketWSASend(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD bytes, DWORD flags, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int WINAPI XSocketWSASendTo(SOCKET socket, LPWSABUF buffers, DWORD count, LPDWORD bytes, DWORD flags, const sockaddr* to, int toLength, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int WINAPI XSocketClose(SOCKET socket);
int WINAPI XSocketSelect(int nfds, fd_set* readSet, fd_set* writeSet, fd_set* exceptSet, const timeval* timeout);
int WINAPI XSocketGetLastError();
void WINAPI XSocketSetLastError(int error);
int WINAPI XSocketBind(SOCKET socket, const struct sockaddr* address, int addressLength);
int WINAPI XSocketConnect(SOCKET socket, const struct sockaddr* address, int addressLength);
int WINAPI XSocketListen(SOCKET socket, int backlog);
SOCKET WINAPI XSocketAccept(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketShutdown(SOCKET socket, int how);
int WINAPI XSocketIOCTLSocket(SOCKET socket, long command, u_long* argument);
int WINAPI XSocketSetSockOpt(SOCKET socket, int level, int option, const char* value, int length);
int WINAPI XSocketGetSockOpt(SOCKET socket, int level, int option, char* value, int* length);
int WINAPI XSocketGetSockName(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketGetPeerName(SOCKET socket, struct sockaddr* address, int* addressLength);
int WINAPI XSocketSend(SOCKET socket, const char* buffer, int length, int flags);
int WINAPI XSocketRecv(SOCKET socket, char* buffer, int length, int flags);
int WINAPI XSocketSendTo(SOCKET socket, const char* buffer, int length, int flags, const struct sockaddr* to, int toLength);
int WINAPI XSocketRecvFrom(SOCKET socket, char* buffer, int length, int flags, struct sockaddr* from, int* fromLength);
u_long WINAPI XSocketInet_Addr(const char* address);
u_long WINAPI XSocketHTONL(u_long value);
u_long WINAPI XSocketNTOHL(u_long value);
u_short WINAPI XSocketHTONS(u_short value);
u_short WINAPI XSocketNTOHS(u_short value);
}
