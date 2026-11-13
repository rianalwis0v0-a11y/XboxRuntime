#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include "xnet_compat.h"
#include "network_compat.h"

namespace {
std::mutex g_xnetMutex;
unsigned g_xnetUsers = 0;
XNetStartupParams g_startupParams{};

void Log(const wchar_t* message) {
    OutputDebugStringW(L"[XboxRuntime:XNet] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}
}

extern "C" int WINAPI XNetStartup(const XNetStartupParams* params) {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (params && params->cfgSizeOfStruct != sizeof(XNetStartupParams))
        return WSAEINVAL;
    if (g_xnetUsers) {
        ++g_xnetUsers;
        return 0;
    }
    if (XboxNetStartup() != 0)
        return WSAENETDOWN;

    std::memset(&g_startupParams, 0, sizeof(g_startupParams));
    if (params) std::memcpy(&g_startupParams, params, sizeof(g_startupParams));
    g_startupParams.cfgSizeOfStruct = sizeof(g_startupParams);
    if (!g_startupParams.cfgSockMaxDgramSockets) g_startupParams.cfgSockMaxDgramSockets = 8;
    if (!g_startupParams.cfgSockMaxStreamSockets) g_startupParams.cfgSockMaxStreamSockets = 32;
    if (!g_startupParams.cfgSockDefaultRecvBufsizeInK) g_startupParams.cfgSockDefaultRecvBufsizeInK = 16;
    if (!g_startupParams.cfgSockDefaultSendBufsizeInK) g_startupParams.cfgSockDefaultSendBufsizeInK = 16;
    if (!g_startupParams.cfgKeyRegMax) g_startupParams.cfgKeyRegMax = 8;
    if (!g_startupParams.cfgSecRegMax) g_startupParams.cfgSecRegMax = 32;
    if (!g_startupParams.cfgQosDataLimitDiv4) g_startupParams.cfgQosDataLimitDiv4 = 64;
    if (!g_startupParams.cfgQosProbeTimeoutInSeconds) g_startupParams.cfgQosProbeTimeoutInSeconds = 2;
    if (!g_startupParams.cfgQosProbeRetries) g_startupParams.cfgQosProbeRetries = 3;
    if (!g_startupParams.cfgQosSrvMaxSimultaneousResponses) g_startupParams.cfgQosSrvMaxSimultaneousResponses = 8;
    if (!g_startupParams.cfgQosPairWaitTimeInSeconds) g_startupParams.cfgQosPairWaitTimeInSeconds = 2;
    g_xnetUsers = 1;
    Log(L"XNet startup complete (transport backend initialized).");
    return 0;
}

extern "C" int WINAPI XNetCleanup() {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return 0;
    if (--g_xnetUsers == 0) {
        XboxNetCleanup();
        std::memset(&g_startupParams, 0, sizeof(g_startupParams));
        Log(L"XNet cleanup complete.");
    }
    return 0;
}

extern "C" int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** output) {
    if (!hostname || !*hostname || !output) return WSAEINVAL;
    *output = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (!g_xnetUsers) return WSANOTINITIALISED;
    }

    XNDNS* result = static_cast<XNDNS*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XNDNS)));
    if (!result) return WSAENOBUFS;

    uint32_t address = 0;
    if (XboxNetResolveIPv4(hostname, &address) == 0) {
        result->iStatus = 0;
        result->cina = 1;
        result->aina[0].s_addr = address;
    } else {
        result->iStatus = WSAHOST_NOT_FOUND;
        result->cina = 0;
    }
    *output = result;
    if (eventHandle) WSASetEvent(eventHandle);
    return 0;
}

extern "C" int WINAPI XNetDnsRelease(XNDNS* result) {
    if (result) HeapFree(GetProcessHeap(), 0, result);
    return 0;
}

extern "C" int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return WSAEFAULT;
    const char* text = inet_ntoa(address);
    if (!text) return WSAEINVAL;
    const size_t length = std::strlen(text);
    if (length + 1 > static_cast<size_t>(capacity)) return WSAEFAULT;
    std::memcpy(buffer, text, length + 1);
    return 0;
}

extern "C" int WINAPI XNetServerToInAddr(IN_ADDR address, DWORD, IN_ADDR* output) {
    if (!output) return WSAEFAULT;
    *output = address;
    return 0;
}

extern "C" int WINAPI XNetInAddrToServer(IN_ADDR address, IN_ADDR* output) {
    if (!output) return WSAEFAULT;
    *output = address;
    return 0;
}

extern "C" DWORD WINAPI XNetGetEthernetLinkStatus() {
    // Report only the host adapter's generic active wired-compatible status;
    // do not claim an Xbox Live sign-in or online address exists.
    return 0x00000001u;
}
