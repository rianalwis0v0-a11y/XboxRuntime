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


// XSocket handles are guest 32-bit tokens. They are deliberately not host
// SOCKET values, because the host runtime may be a separate 64-bit process.
extern "C" SOCKET WINAPI XSocketCreate(int af, int type, int protocol) {
    if (af != AF_INET) return INVALID_SOCKET;
    if (type == SOCK_STREAM) {
        if (protocol != 0 && protocol != IPPROTO_TCP) return INVALID_SOCKET;
        const uint32_t handle = XboxNetSocketCreate(1);
        return handle ? static_cast<SOCKET>(handle) : INVALID_SOCKET;
    }
    if (type == SOCK_DGRAM) {
        if (protocol != 0 && protocol != IPPROTO_UDP && protocol != 254) return INVALID_SOCKET;
        const uint32_t handle = XboxNetSocketCreate(2);
        return handle ? static_cast<SOCKET>(handle) : INVALID_SOCKET;
    }
    return INVALID_SOCKET;
}

extern "C" int WINAPI XSocketClose(SOCKET socket) {
    if (socket == INVALID_SOCKET || socket == 0) return SOCKET_ERROR;
    return XboxNetSocketClose(static_cast<uint32_t>(socket)) == 0 ? 0 : SOCKET_ERROR;
}

extern "C" int WINAPI XSocketBind(SOCKET socket, const sockaddr* address, int addressLength) {
    if (socket == INVALID_SOCKET || !address || addressLength < static_cast<int>(sizeof(sockaddr_in)) ||
        address->sa_family != AF_INET) return SOCKET_ERROR;
    return XboxNetSocketBindAddress(static_cast<uint32_t>(socket), address,
        static_cast<uint32_t>(addressLength));
}

extern "C" int WINAPI XSocketConnect(SOCKET socket, const sockaddr* address, int addressLength) {
    if (socket == INVALID_SOCKET || !address || addressLength < static_cast<int>(sizeof(sockaddr_in)) ||
        address->sa_family != AF_INET) return SOCKET_ERROR;
    return XboxNetSocketConnectAddress(static_cast<uint32_t>(socket), address,
        static_cast<uint32_t>(addressLength));
}

extern "C" int WINAPI XSocketListen(SOCKET socket, int backlog) {
    if (socket == INVALID_SOCKET) return SOCKET_ERROR;
    return XboxNetSocketListen(static_cast<uint32_t>(socket), backlog);
}

extern "C" SOCKET WINAPI XSocketAccept(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET) return INVALID_SOCKET;
    uint32_t length = addressLength && *addressLength > 0
        ? static_cast<uint32_t>(*addressLength) : 0;
    const uint32_t accepted = XboxNetSocketAccept(static_cast<uint32_t>(socket),
        address, addressLength ? &length : nullptr);
    if (!accepted) return INVALID_SOCKET;
    if (addressLength && address) *addressLength = static_cast<int>(length);
    return static_cast<SOCKET>(accepted);
}

extern "C" int WINAPI XSocketShutdown(SOCKET socket, int how) {
    if (socket == INVALID_SOCKET) return SOCKET_ERROR;
    return XboxNetSocketShutdown(static_cast<uint32_t>(socket), how);
}

extern "C" int WINAPI XSocketIOCTLSocket(SOCKET socket, long command, u_long* argument) {
    if (socket == INVALID_SOCKET || !argument || command != FIONBIO) return SOCKET_ERROR;
    return XboxNetSocketSetNonBlocking(static_cast<uint32_t>(socket), *argument != 0);
}

extern "C" int WINAPI XSocketSetSockOpt(SOCKET socket, int level, int option, const char* value, int length) {
    if (socket == INVALID_SOCKET || (!value && length) || length < 0) return SOCKET_ERROR;
    return XboxNetSocketSetOption(static_cast<uint32_t>(socket), level, option, value, length);
}

extern "C" int WINAPI XSocketGetSockOpt(SOCKET socket, int level, int option, char* value, int* length) {
    if (socket == INVALID_SOCKET || !value || !length) return SOCKET_ERROR;
    return XboxNetSocketGetOption(static_cast<uint32_t>(socket), level, option, value, length);
}

extern "C" int WINAPI XSocketGetSockName(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET || !address || !addressLength || *addressLength < static_cast<int>(sizeof(sockaddr_in)))
        return SOCKET_ERROR;
    uint32_t length = static_cast<uint32_t>(*addressLength);
    const int result = XboxNetSocketGetName(static_cast<uint32_t>(socket), address, &length);
    if (result == 0) *addressLength = static_cast<int>(length);
    return result;
}

extern "C" int WINAPI XSocketGetPeerName(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET || !address || !addressLength || *addressLength < static_cast<int>(sizeof(sockaddr_in)))
        return SOCKET_ERROR;
    // The transport token is intentionally opaque; use a small internal helper
    // rather than treating it as a native host SOCKET.
    return SOCKET_ERROR; // Peer-name bridge is not implemented yet.
}

extern "C" int WINAPI XSocketSend(SOCKET socket, const char* buffer, int length, int flags) {
    if (socket == INVALID_SOCKET || length < 0 || (!buffer && length)) return SOCKET_ERROR;
    (void)flags;
    return XboxNetSocketSend(static_cast<uint32_t>(socket), buffer, static_cast<uint32_t>(length), flags);
}

extern "C" int WINAPI XSocketRecv(SOCKET socket, char* buffer, int length, int flags) {
    if (socket == INVALID_SOCKET || length < 0 || (!buffer && length)) return SOCKET_ERROR;
    return XboxNetSocketReceive(static_cast<uint32_t>(socket), buffer, static_cast<uint32_t>(length), flags);
}

extern "C" int WINAPI XSocketSendTo(SOCKET socket, const char* buffer, int length, int flags,
    const sockaddr* to, int toLength) {
    if (socket == INVALID_SOCKET || length < 0 || (!buffer && length) || !to ||
        toLength < static_cast<int>(sizeof(sockaddr_in)) || to->sa_family != AF_INET)
        return SOCKET_ERROR;
    const sockaddr_in* target = reinterpret_cast<const sockaddr_in*>(to);
    return XboxNetSocketSendTo(static_cast<uint32_t>(socket), buffer,
        static_cast<uint32_t>(length), target->sin_addr.s_addr, ntohs(target->sin_port), flags);
}

extern "C" int WINAPI XSocketRecvFrom(SOCKET socket, char* buffer, int length, int flags,
    sockaddr* from, int* fromLength) {
    if (socket == INVALID_SOCKET || length < 0 || (!buffer && length)) return SOCKET_ERROR;
    uint32_t address = 0;
    uint16_t port = 0;
    const int result = XboxNetSocketReceiveFrom(static_cast<uint32_t>(socket), buffer,
        static_cast<uint32_t>(length), &address, &port, flags);
    if (result >= 0 && from && fromLength && *fromLength >= static_cast<int>(sizeof(sockaddr_in))) {
        sockaddr_in output{};
        output.sin_family = AF_INET;
        output.sin_addr.s_addr = address;
        output.sin_port = htons(port);
        std::memcpy(from, &output, sizeof(output));
        *fromLength = sizeof(output);
    }
    return result;
}

extern "C" u_long WINAPI XSocketHTONL(u_long value) { return htonl(value); }
extern "C" u_long WINAPI XSocketNTOHL(u_long value) { return ntohl(value); }
extern "C" u_short WINAPI XSocketHTONS(u_short value) { return htons(value); }
extern "C" u_short WINAPI XSocketNTOHS(u_short value) { return ntohs(value); }
