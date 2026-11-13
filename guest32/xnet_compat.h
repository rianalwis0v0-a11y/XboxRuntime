#pragma once
#define WIN32_LEAN_AND_MEAN
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
#pragma pack(pop)

static_assert(sizeof(XNetStartupParams) == 13, "XNetStartupParams ABI size");
static_assert(sizeof(XNADDR) == 36, "XNADDR ABI size");
static_assert(sizeof(XNDNS) == 40, "XNDNS ABI size");

extern "C" {
int WINAPI XNetStartup(const XNetStartupParams* params);
int WINAPI XNetCleanup();
int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** result);
int WINAPI XNetDnsRelease(XNDNS* result);
int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity);
int WINAPI XNetServerToInAddr(IN_ADDR address, DWORD serviceId, IN_ADDR* output);
int WINAPI XNetInAddrToServer(IN_ADDR address, IN_ADDR* output);
DWORD WINAPI XNetGetEthernetLinkStatus();
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
