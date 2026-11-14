#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include <iphlpapi.h>
#include <new>
#include <unordered_set>
#include <bcrypt.h>
#include "xnet_compat.h"
#include "network_compat.h"

namespace {
std::mutex g_xnetMutex;
unsigned g_xnetUsers = 0;
XNetStartupParams g_startupParams{};
struct RegisteredKey { XNKID id; XNKEY key; };
std::vector<RegisteredKey> g_registeredKeys;
std::unordered_set<uint32_t> g_xnetSocketTokens;

void Log(const wchar_t* message) {
    OutputDebugStringW(L"[XboxRuntime:XNet] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}
}

extern "C" int WINAPI XNetStartup(const XNetStartupParams* params) {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (params && params->cfgSizeOfStruct != sizeof(XNetStartupParams))
        return WSAEFAULT;
    if (g_xnetUsers) {
        ++g_xnetUsers;
        return 0;
    }
    if (XboxNetStartup() != 0)
        return WSAENETDOWN;

    std::memset(&g_startupParams, 0, sizeof(g_startupParams));
    if (params) std::memcpy(&g_startupParams, params, sizeof(g_startupParams));
    g_startupParams.cfgSizeOfStruct = sizeof(g_startupParams);
    if (!g_startupParams.cfgPrivatePoolSizeInPages) g_startupParams.cfgPrivatePoolSizeInPages = 12;
    if (!g_startupParams.cfgEnetReceiveQueueLength) g_startupParams.cfgEnetReceiveQueueLength = 8;
    if (!g_startupParams.cfgIpFragMaxSimultaneous) g_startupParams.cfgIpFragMaxSimultaneous = 4;
    if (!g_startupParams.cfgIpFragMaxPacketDiv256) g_startupParams.cfgIpFragMaxPacketDiv256 = 8;
    if (!g_startupParams.cfgSockMaxSockets) g_startupParams.cfgSockMaxSockets = 64;
    if (!g_startupParams.cfgSockDefaultRecvBufsizeInK) g_startupParams.cfgSockDefaultRecvBufsizeInK = 16;
    if (!g_startupParams.cfgSockDefaultSendBufsizeInK) g_startupParams.cfgSockDefaultSendBufsizeInK = 16;
    if (!g_startupParams.cfgKeyRegMax) g_startupParams.cfgKeyRegMax = 4;
    if (!g_startupParams.cfgSecRegMax) g_startupParams.cfgSecRegMax = 32;
    if (!g_startupParams.cfgQosDataLimitDiv4) g_startupParams.cfgQosDataLimitDiv4 = 128;
    g_xnetUsers = 1;
    Log(L"XNet startup complete (transport backend initialized).");
    return 0;
}

extern "C" int WINAPI XNetCleanup() {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return 0;
    if (--g_xnetUsers == 0) {
        XboxNetCleanup();
        for (auto& entry : g_registeredKeys) SecureZeroMemory(&entry, sizeof(entry));
        g_registeredKeys.clear();
        g_xnetSocketTokens.clear(); // XboxNetCleanup invalidates the backend token table too.
        std::memset(&g_startupParams, 0, sizeof(g_startupParams));
        Log(L"XNet cleanup complete.");
    }
    return 0;
}

namespace {
struct DnsLookupWork {
    char* hostname;
    XNDNS* result;
    WSAEVENT eventHandle;
};

DWORD WINAPI CompleteDnsLookup(void* parameter) {
    auto* work = static_cast<DnsLookupWork*>(parameter);
    uint32_t address = 0;
    if (XboxNetResolveIPv4(work->hostname, &address) == 0) {
        work->result->iStatus = 0;
        work->result->cina = 1;
        work->result->aina[0].s_addr = address;
    } else {
        work->result->iStatus = WSAHOST_NOT_FOUND;
        work->result->cina = 0;
    }
    if (work->eventHandle) WSASetEvent(work->eventHandle);
    HeapFree(GetProcessHeap(), 0, work->hostname);
    HeapFree(GetProcessHeap(), 0, work);
    XboxNetCleanup(); // Keep Winsock alive until the resolver has finished.
    return 0;
}
}

extern "C" int WINAPI XNetCreateKey(XNKID* keyId, XNKEY* key) {
    if (!keyId || !key) return WSAEFAULT;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, keyId->ab, sizeof(keyId->ab),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
        !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, key->ab, sizeof(key->ab),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        SecureZeroMemory(keyId, sizeof(*keyId));
        SecureZeroMemory(key, sizeof(*key));
        return WSAENOBUFS;
    }
    // XDK XNetCreateKey creates a system-link (peer-to-peer) XNKID.
    keyId->ab[0] &= static_cast<BYTE>(~0xF0u);
    return 0;
}

extern "C" int WINAPI XNetRegisterKey(const XNKID* keyId, const XNKEY* key) {
    if (!keyId || !key) return WSAEFAULT;
    // The secure XNet peer registration path accepts system-link and online-peer
    // identifiers. This backend currently implements only the system-link
    // registration table; it does not claim to perform the XNet DH/security handshake.
    if ((keyId->ab[0] & 0xC0u) != 0x00u) return WSAEINVAL;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    for (const auto& entry : g_registeredKeys) {
        if (std::memcmp(entry.id.ab, keyId->ab, sizeof(keyId->ab)) == 0)
            return WSAEALREADY;
    }
    const size_t limit = g_startupParams.cfgKeyRegMax ? g_startupParams.cfgKeyRegMax : 4;
    if (g_registeredKeys.size() >= limit) return WSAENOMORE;
    try {
        RegisteredKey entry{};
        entry.id = *keyId;
        entry.key = *key;
        g_registeredKeys.push_back(entry);
    } catch (...) {
        return WSAENOBUFS;
    }
    return 0;
}

extern "C" int WINAPI XNetUnregisterKey(const XNKID* keyId) {
    if (!keyId) return WSAEFAULT;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    for (auto it = g_registeredKeys.begin(); it != g_registeredKeys.end(); ++it) {
        if (std::memcmp(it->id.ab, keyId->ab, sizeof(keyId->ab)) == 0) {
            SecureZeroMemory(&*it, sizeof(*it));
            g_registeredKeys.erase(it);
            return 0;
        }
    }
    return WSAEINVAL;
}

extern "C" int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** output) {
    if (!hostname || !*hostname || !output) return WSAEINVAL;
    *output = nullptr;
    const size_t hostnameLength = std::strlen(hostname);
    if (hostnameLength > 255) return WSAEINVAL;
    if (eventHandle && !WSAResetEvent(eventHandle)) return WSAGetLastError();

    XNDNS* result = static_cast<XNDNS*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XNDNS)));
    auto* work = static_cast<DnsLookupWork*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DnsLookupWork)));
    char* nameCopy = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, hostnameLength + 1));
    if (!result || !work || !nameCopy) {
        if (result) HeapFree(GetProcessHeap(), 0, result);
        if (work) HeapFree(GetProcessHeap(), 0, work);
        if (nameCopy) HeapFree(GetProcessHeap(), 0, nameCopy);
        return WSAENOBUFS;
    }
    std::memcpy(nameCopy, hostname, hostnameLength + 1);

    // Acquire the operation's Winsock reference while holding the XNet
    // lifecycle lock, so a concurrent final XNetCleanup cannot slip between
    // the initialized check and this reference acquisition.
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (!g_xnetUsers || XboxNetStartup() != 0) {
            HeapFree(GetProcessHeap(), 0, result);
            HeapFree(GetProcessHeap(), 0, work);
            HeapFree(GetProcessHeap(), 0, nameCopy);
            return WSANOTINITIALISED;
        }
    }
    work->hostname = nameCopy;
    work->result = result;
    work->eventHandle = eventHandle;
    HANDLE thread = CreateThread(nullptr, 0, CompleteDnsLookup, work, 0, nullptr);
    if (!thread) {
        XboxNetCleanup();
        HeapFree(GetProcessHeap(), 0, result);
        HeapFree(GetProcessHeap(), 0, work);
        HeapFree(GetProcessHeap(), 0, nameCopy);
        return WSAENOBUFS;
    }
    CloseHandle(thread);
    *output = result;
    return 0;
}

extern "C" int WINAPI XNetDnsRelease(XNDNS* result) {
    if (result) HeapFree(GetProcessHeap(), 0, result);
    return 0;
}

extern "C" int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return WSAEFAULT;
    if (!InetNtopA(AF_INET, &address, buffer, static_cast<size_t>(capacity))) {
        const int error = WSAGetLastError();
        return error ? error : WSAEINVAL;
    }
    return 0;
}

extern "C" int WINAPI XNetRandom(BYTE* buffer, UINT length) {
    if (!buffer && length) return WSAEFAULT;
    if (!length) return 0;
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer, length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ? 0 : WSAENOBUFS;
}

extern "C" int WINAPI XNetXnAddrToInAddr(const XNADDR* address,
    const XNKID* keyId, IN_ADDR* output) {
    if (!address || !keyId || !output) return WSAEFAULT;
    // Online XNADDR resolution requires XNet key registration/security state.
    // Never fabricate an IP address when that state is unavailable.
    return WSAEOPNOTSUPP;
}

extern "C" int WINAPI XNetServerToInAddr(IN_ADDR, DWORD, IN_ADDR* output) {
    if (!output) return WSAEFAULT;
    // Service-ID address resolution is not implemented yet.
    return WSAEOPNOTSUPP;
}

extern "C" int WINAPI XNetInAddrToXnAddr(const IN_ADDR, XNADDR* output,
    XNKID* keyId) {
    if (!output || !keyId) return WSAEFAULT;
    // Reverse mapping requires registered online address/key state.
    return WSAEOPNOTSUPP;
}

extern "C" int WINAPI XNetUnregisterInAddr(const IN_ADDR) {
    return WSAEOPNOTSUPP;
}

namespace {
PIP_ADAPTER_ADDRESSES FindUsableIPv4Adapter(std::vector<BYTE>& storage) {
    ULONG bytes = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, nullptr, &bytes);
    if (!bytes) return nullptr;
    storage.resize(bytes);
    auto* first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, first, &bytes) != NO_ERROR) return nullptr;
    for (auto* adapter = first; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr && unicast->Address.lpSockaddr->sa_family == AF_INET) {
                const auto* ip = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                if (ip->sin_addr.s_addr != htonl(INADDR_ANY) && ip->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) return adapter;
            }
        }
    }
    return nullptr;
}
}

extern "C" DWORD WINAPI XNetGetEthernetLinkStatus() {
    std::vector<BYTE> storage;
    auto* adapter = FindUsableIPv4Adapter(storage);
    if (!adapter) return 0;
    DWORD status = XNET_ETHERNET_LINK_ACTIVE;
    if (adapter->TransmitLinkSpeed >= 100000000ull) status |= XNET_ETHERNET_LINK_100MBPS;
    else if (adapter->TransmitLinkSpeed > 0) status |= XNET_ETHERNET_LINK_10MBPS;
    if (adapter->IfType == IF_TYPE_IEEE80211) status |= XNET_ETHERNET_LINK_WIRELESS;
    // IP Helper does not expose duplex mode on this adapter record; leave the
    // duplex bits clear rather than inventing full/half duplex state.
    return status;
}

extern "C" DWORD WINAPI XNetGetTitleXnAddr(XNADDR* output) {
    if (!output) return XNET_GET_XNADDR_NONE;
    std::memset(output, 0, sizeof(*output));
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (!g_xnetUsers) return XNET_GET_XNADDR_NONE;
    }
    std::vector<BYTE> storage;
    auto* adapter = FindUsableIPv4Adapter(storage);
    if (!adapter) return XNET_GET_XNADDR_NONE;
    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
        if (unicast->Address.lpSockaddr && unicast->Address.lpSockaddr->sa_family == AF_INET) {
            const auto* ip = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            if (ip->sin_addr.s_addr != htonl(INADDR_ANY) && ip->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) { output->ina = ip->sin_addr; break; }
        }
    }
    if (adapter->PhysicalAddressLength >= sizeof(output->abEnet)) std::memcpy(output->abEnet, adapter->PhysicalAddress, sizeof(output->abEnet));
    DWORD flags = XNET_GET_XNADDR_ETHERNET |
        (adapter->Dhcpv4Enabled ? XNET_GET_XNADDR_DHCP : XNET_GET_XNADDR_STATIC);
    if (adapter->FirstGatewayAddress) flags |= XNET_GET_XNADDR_GATEWAY;
    if (adapter->FirstDnsServerAddress) flags |= XNET_GET_XNADDR_DNS;
    return flags;
}


extern "C" DWORD WINAPI XNetGetDebugXnAddr(XNADDR* output) {
    if (output) std::memset(output, 0, sizeof(*output));
    // Debug XNADDR is devkit-specific and must not be synthesized on retail hosts.
    return XNET_GET_XNADDR_NONE;
}

// XSocket handles are guest 32-bit tokens. They are deliberately not host
// SOCKET values, because the host runtime may be a separate 64-bit process.
extern "C" SOCKET WINAPI XSocketCreate(int af, int type, int protocol) {
    if (af != AF_INET) { WSASetLastError(WSAEAFNOSUPPORT); return INVALID_SOCKET; }
    int transport = 0;
    if (type == SOCK_STREAM && (protocol == 0 || protocol == IPPROTO_TCP)) transport = 1;
    else if (type == SOCK_DGRAM && (protocol == 0 || protocol == IPPROTO_UDP)) transport = 2;
    else { WSASetLastError(WSAEPROTONOSUPPORT); return INVALID_SOCKET; }

    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) { WSASetLastError(WSANOTINITIALISED); return INVALID_SOCKET; }
    const size_t limit = g_startupParams.cfgSockMaxSockets ? g_startupParams.cfgSockMaxSockets : 64;
    if (g_xnetSocketTokens.size() >= limit) { WSASetLastError(WSAEMFILE); return INVALID_SOCKET; }
    const uint32_t handle = XboxNetSocketCreate(transport);
    if (!handle) { WSASetLastError(WSAGetLastError()); return INVALID_SOCKET; }
    try { g_xnetSocketTokens.insert(handle); }
    catch (...) {
        XboxNetSocketClose(handle);
        WSASetLastError(WSAENOBUFS);
        return INVALID_SOCKET;
    }
    return static_cast<SOCKET>(handle);
}

extern "C" int WINAPI XSocketClose(SOCKET socket) {
    if (socket == INVALID_SOCKET || socket == 0) { WSASetLastError(WSAENOTSOCK); return SOCKET_ERROR; }
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (g_xnetSocketTokens.erase(static_cast<uint32_t>(socket)) == 0) {
            WSASetLastError(WSAENOTSOCK);
            return SOCKET_ERROR;
        }
    }
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
    if (address && !addressLength) {
        WSASetLastError(WSAEFAULT);
        return INVALID_SOCKET;
    }
    if (addressLength && *addressLength < 0) {
        WSASetLastError(WSAEINVAL);
        return INVALID_SOCKET;
    }
    uint32_t length = addressLength && *addressLength > 0
        ? static_cast<uint32_t>(*addressLength) : 0;
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (!g_xnetUsers || g_xnetSocketTokens.find(static_cast<uint32_t>(socket)) == g_xnetSocketTokens.end()) {
            WSASetLastError(WSAENOTSOCK);
            return INVALID_SOCKET;
        }
    }
    const uint32_t accepted = XboxNetSocketAccept(static_cast<uint32_t>(socket),
        address, addressLength ? &length : nullptr);
    if (!accepted) return INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        const size_t limit = g_startupParams.cfgSockMaxSockets ? g_startupParams.cfgSockMaxSockets : 64;
        if (!g_xnetUsers || g_xnetSocketTokens.size() >= limit) {
            XboxNetSocketClose(accepted);
            WSASetLastError(g_xnetUsers ? WSAEMFILE : WSANOTINITIALISED);
            return INVALID_SOCKET;
        }
        try { g_xnetSocketTokens.insert(accepted); }
        catch (...) {
            XboxNetSocketClose(accepted);
            WSASetLastError(WSAENOBUFS);
            return INVALID_SOCKET;
        }
    }
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

extern "C" int WINAPI XSocketSelect(int nfds, fd_set* readfds, fd_set* writefds,
    fd_set* exceptfds, const timeval* timeout) {
    return XboxNetSocketSelect(nfds, readfds, writefds, exceptfds, timeout);
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
    uint32_t length = static_cast<uint32_t>(*addressLength);
    const int result = XboxNetSocketGetPeerName(static_cast<uint32_t>(socket), address, &length);
    if (result == 0) *addressLength = static_cast<int>(length);
    return result;
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
    if (from && !fromLength) {
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    if (fromLength && *fromLength < 0) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    if (from && *fromLength < static_cast<int>(sizeof(sockaddr_in))) {
        WSASetLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
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


extern "C" int WINAPI XboxWSAStartup(WORD versionRequested, LPWSADATA data) {
    if (!data) return WSAEFAULT;
    const int startup = XboxNetStartup();
    if (startup != 0) return WSASYSNOTREADY;
    std::memset(data, 0, sizeof(*data));
    data->wVersion = versionRequested;
    data->wHighVersion = MAKEWORD(2, 2);
    std::strncpy(data->szDescription, "XboxRuntime Winsock", sizeof(data->szDescription) - 1);
    std::strncpy(data->szSystemStatus, "Running", sizeof(data->szSystemStatus) - 1);
    return 0;
}

extern "C" int WINAPI XboxWSACleanup() {
    // XboxNetCleanup is intentionally void: it decrements the shared startup
    // reference count and has no Winsock-style error return.
    XboxNetCleanup();
    return 0;
}

extern "C" WSAEVENT WINAPI XboxWSACreateEvent() { return WSACreateEvent(); }
extern "C" BOOL WINAPI XboxWSACloseEvent(WSAEVENT eventHandle) { return WSACloseEvent(eventHandle); }
extern "C" BOOL WINAPI XboxWSASetEvent(WSAEVENT eventHandle) { return WSASetEvent(eventHandle); }
extern "C" BOOL WINAPI XboxWSAResetEvent(WSAEVENT eventHandle) { return WSAResetEvent(eventHandle); }
extern "C" DWORD WINAPI XboxWSAWaitForMultipleEvents(DWORD count, const WSAEVENT* events, BOOL waitAll, DWORD timeout, BOOL alertable) {
    return WSAWaitForMultipleEvents(count, events, waitAll, timeout, alertable);
}
extern "C" int WINAPI XboxWSAGetLastError() { return WSAGetLastError(); }
extern "C" void WINAPI XboxWSASetLastError(int error) { WSASetLastError(error); }
extern "C" ULONG WINAPI XboxWSAInetAddr(const char* address) { return address ? inet_addr(address) : INADDR_NONE; }
