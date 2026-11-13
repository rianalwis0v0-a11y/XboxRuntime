#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <cstdint>

// Original Xbox XNet ABI structures. Keep these fixed-width: this file is
// compiled only into the dedicated 32-bit guest process.
#pragma pack(push, 4)
struct XboxXNetStartupParams {
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
struct XboxXnAddr {
    IN_ADDR ina;
    IN_ADDR inaOnline;
    WORD wPortOnline;
    BYTE abEnet[6];
    BYTE abOnline[20];
};
struct XboxXnDns {
    INT iStatus;
    UINT cina;
    IN_ADDR aina[8];
};
#pragma pack(pop)

static_assert(sizeof(XboxXNetStartupParams) == 13, "XNetStartupParams ABI size");
static_assert(sizeof(XboxXnAddr) == 36, "XNADDR ABI size");
static_assert(sizeof(XboxXnDns) == 40, "XNDNS ABI size");

extern "C" {
int WINAPI XboxXNetStartup(const XboxXNetStartupParams* params);
int WINAPI XboxXNetCleanup();
int WINAPI XboxXNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XboxXnDns** result);
int WINAPI XboxXNetDnsRelease(XboxXnDns* result);
int WINAPI XboxXNetInAddrToString(IN_ADDR address, char* buffer, int capacity);
int WINAPI XboxXNetServerToInAddr(IN_ADDR address, DWORD serviceId, IN_ADDR* output);
int WINAPI XboxXNetInAddrToServer(IN_ADDR address, IN_ADDR* output);
DWORD WINAPI XboxXNetGetEthernetLinkStatus();
}
