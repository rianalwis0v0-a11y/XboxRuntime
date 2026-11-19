#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include <iphlpapi.h>
#include <new>
#include <thread>
#include <string>
#include "xnet_compat.h"
#include "network_compat.h"

namespace {
int SocketFailure(int error) {
    WSASetLastError(error);
    return SOCKET_ERROR;
}
SOCKET SocketHandleFailure(int error) {
    WSASetLastError(error);
    return INVALID_SOCKET;
}
std::mutex g_xnetMutex;
unsigned g_xnetUsers = 0;
XNetStartupParams g_startupParams{};
struct RegisteredKey {
    XNKID id;
    XNKEY key;
};
std::vector<RegisteredKey> g_registeredKeys;
struct AddressMapping { IN_ADDR address; XNADDR xnaddr; XNKID keyId; bool hasKeyId; };
std::vector<AddressMapping> g_addressMappings;

bool IsIPv4OnLocalSubnet(IN_ADDR candidate) {
    if (candidate.s_addr == htonl(INADDR_LOOPBACK)) return true;
    ULONG bytes = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, nullptr, &bytes) != ERROR_BUFFER_OVERFLOW || !bytes) return false;
    std::vector<BYTE> storage;
    try { storage.resize(bytes); } catch (...) { return false; }
    auto* first = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(storage.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
            nullptr, first, &bytes) != NO_ERROR) return false;
    for (auto* adapter = first; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* local = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            const ULONG prefix = unicast->OnLinkPrefixLength;
            const ULONG mask = prefix == 0 ? 0 : htonl(0xFFFFFFFFu << (32 - prefix));
            if ((local->sin_addr.s_addr & mask) == (candidate.s_addr & mask)) return true;
        }
    }
    return false;
}

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
        for (auto& entry : g_registeredKeys)
            SecureZeroMemory(&entry, sizeof(entry));
        g_registeredKeys.clear();
        g_addressMappings.clear();
        std::memset(&g_startupParams, 0, sizeof(g_startupParams));
        Log(L"XNet cleanup complete.");
    }
    return 0;
}

namespace {
void ReleaseXNetWorkerReference() {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (g_xnetUsers && --g_xnetUsers == 0) {
        XboxNetCleanup();
        for (auto& entry : g_registeredKeys)
            SecureZeroMemory(&entry, sizeof(entry));
        g_registeredKeys.clear();
        std::memset(&g_startupParams, 0, sizeof(g_startupParams));
        Log(L"XNet cleanup completed after outstanding DNS work.");
    }
}
}

extern "C" int WINAPI XNetDnsLookup(const char* hostname, WSAEVENT eventHandle, XNDNS** output) {
    if (!hostname || !*hostname || !output) return WSAEINVAL;
    *output = nullptr;

    // The caller owns the returned XNDNS and must keep its pointer valid until
    // the completion event is signaled. Allocate it before queuing the worker.
    XNDNS* result = static_cast<XNDNS*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(XNDNS)));
    if (!result) return WSAENOBUFS;
    result->iStatus = WSAEINPROGRESS;
    *output = result;

    std::string hostCopy;
    try {
        hostCopy.assign(hostname);
    } catch (...) {
        *output = nullptr;
        HeapFree(GetProcessHeap(), 0, result);
        return WSAENOBUFS;
    }

    {
        std::lock_guard<std::mutex> lock(g_xnetMutex);
        if (!g_xnetUsers) {
            *output = nullptr;
            HeapFree(GetProcessHeap(), 0, result);
            return WSANOTINITIALISED;
        }
        // Keep Winsock initialized even if the title calls XNetCleanup while
        // this asynchronous lookup is still in flight.
        ++g_xnetUsers;
    }

    try {
        std::thread([result, hostCopy = std::move(hostCopy), eventHandle]() {
            uint32_t address = 0;
            if (XboxNetResolveIPv4(hostCopy.c_str(), &address) == 0) {
                result->iStatus = 0;
                result->cina = 1;
                result->aina[0].s_addr = address;
            } else {
                result->iStatus = WSAHOST_NOT_FOUND;
                result->cina = 0;
            }
            // Signal only after all result fields are published.
            if (eventHandle) WSASetEvent(eventHandle);
            ReleaseXNetWorkerReference();
        }).detach();
    } catch (...) {
        ReleaseXNetWorkerReference();
        *output = nullptr;
        HeapFree(GetProcessHeap(), 0, result);
        return WSAENOBUFS;
    }
    return 0;
}

extern "C" int WINAPI XNetDnsRelease(XNDNS* result) {
    if (result) HeapFree(GetProcessHeap(), 0, result);
    return 0;
}

extern "C" int WINAPI XNetRandom(BYTE* buffer, UINT length) {
    if (!buffer && length) return WSAEFAULT;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    if (!length) return 0;
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer, length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ? 0 : WSAENOBUFS;
}

extern "C" int WINAPI XNetCreateKey(XNKID* keyId, XNKEY* key) {
    if (!keyId || !key) return WSAEFAULT;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    SecureZeroMemory(keyId, sizeof(*keyId));
    SecureZeroMemory(key, sizeof(*key));
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, keyId->ab, sizeof(keyId->ab),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG)) ||
        !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, key->ab, sizeof(key->ab),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        SecureZeroMemory(keyId, sizeof(*keyId));
        SecureZeroMemory(key, sizeof(*key));
        return WSAENOBUFS;
    }
    // XNetCreateKey creates a system-link key identifier. Online key types
    // require the separate XNet security/identity handshake and are not faked.
    keyId->ab[0] &= 0x0Fu;
    return 0;
}

extern "C" int WINAPI XNetRegisterKey(const XNKID* keyId, const XNKEY* key) {
    if (!keyId || !key) return WSAEFAULT;
    if ((keyId->ab[0] & 0xF0u) != 0) return WSAEINVAL;
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) return WSANOTINITIALISED;
    for (const auto& entry : g_registeredKeys) {
        if (std::memcmp(entry.id.ab, keyId->ab, sizeof(keyId->ab)) == 0)
            return WSAEALREADY;
    }
    const size_t limit = g_startupParams.cfgKeyRegMax
        ? g_startupParams.cfgKeyRegMax : 8;
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
            for (auto mapIt = g_addressMappings.begin(); mapIt != g_addressMappings.end();) {
                if (mapIt->hasKeyId && std::memcmp(mapIt->keyId.ab, keyId->ab, sizeof(keyId->ab)) == 0)
                    mapIt = g_addressMappings.erase(mapIt);
                else ++mapIt;
            }
            return 0;
        }
    }
    return WSAEINVAL;
}

extern "C" int WINAPI XNetXnAddrToInAddr(const XNADDR* address, const XNKID* keyId, IN_ADDR* output) {
    if (!address || !output) { WSASetLastError(WSAEFAULT); return WSAEFAULT; }
    IN_ADDR selected = address->ina;
    if (selected.s_addr == 0 || !IsIPv4OnLocalSubnet(selected)) {
        if (address->inaOnline.s_addr) selected = address->inaOnline;
    }
    if (selected.s_addr == 0 || selected.s_addr == htonl(INADDR_ANY)) {
        WSASetLastError(WSAEINVAL); return WSAEINVAL;
    }
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) { WSASetLastError(WSANOTINITIALISED); return WSANOTINITIALISED; }
    if (keyId) {
        bool registered = false;
        for (const auto& entry : g_registeredKeys)
            if (std::memcmp(entry.id.ab, keyId->ab, sizeof(keyId->ab)) == 0) { registered = true; break; }
        if (!registered) { WSASetLastError(WSAEINVAL); return WSAEINVAL; }
    }
    for (auto& mapping : g_addressMappings) {
        if (mapping.address.s_addr == selected.s_addr) {
            mapping.xnaddr = *address;
            mapping.hasKeyId = keyId != nullptr;
            if (keyId) mapping.keyId = *keyId;
            *output = selected;
            return 0;
        }
    }
    try {
        AddressMapping mapping{};
        mapping.address = selected;
        mapping.xnaddr = *address;
        mapping.hasKeyId = keyId != nullptr;
        if (keyId) mapping.keyId = *keyId;
        g_addressMappings.push_back(mapping);
    } catch (...) {
        WSASetLastError(WSAENOBUFS); return WSAENOBUFS;
    }
    *output = selected;
    return 0;
}

extern "C" int WINAPI XNetInAddrToXnAddr(IN_ADDR address, XNADDR* output, XNKID* keyId) {
    if (!output) { WSASetLastError(WSAEFAULT); return WSAEFAULT; }
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) { WSASetLastError(WSANOTINITIALISED); return WSANOTINITIALISED; }
    for (const auto& mapping : g_addressMappings) {
        if (mapping.address.s_addr == address.s_addr) {
            *output = mapping.xnaddr;
            if (keyId) {
                if (mapping.hasKeyId) *keyId = mapping.keyId;
                else SecureZeroMemory(keyId, sizeof(*keyId));
            }
            return 0;
        }
    }
    WSASetLastError(WSAEINVAL);
    return WSAEINVAL;
}

extern "C" int WINAPI XNetUnregisterInAddr(IN_ADDR address) {
    std::lock_guard<std::mutex> lock(g_xnetMutex);
    if (!g_xnetUsers) { WSASetLastError(WSANOTINITIALISED); return WSANOTINITIALISED; }
    for (auto it = g_addressMappings.begin(); it != g_addressMappings.end(); ++it) {
        if (it->address.s_addr == address.s_addr) {
            SecureZeroMemory(&*it, sizeof(*it));
            g_addressMappings.erase(it);
            return 0;
        }
    }
    WSASetLastError(WSAEINVAL);
    return WSAEINVAL;
}

extern "C" int WINAPI XNetInAddrToString(IN_ADDR address, char* buffer, int capacity) {
    if (!buffer || capacity <= 0) return WSAEFAULT;
    // InetNtop writes directly to the caller's buffer and is thread-safe,
    // unlike inet_ntoa's shared static storage.
    if (!InetNtopA(AF_INET, &address, buffer, static_cast<size_t>(capacity)))
        return WSAGetLastError();
    return 0;
}

extern "C" int WINAPI XNetServerToInAddr(IN_ADDR, DWORD, IN_ADDR* output) {
    if (!output) return WSAEFAULT;
    // Service-ID address translation requires the Xbox/Insignia service map.
    // Returning the input address here would falsely claim that mapping worked.
    WSASetLastError(WSAEOPNOTSUPP);
    return WSAEOPNOTSUPP;
}

extern "C" int WINAPI XNetInAddrToServer(IN_ADDR, IN_ADDR* output) {
    if (!output) return WSAEFAULT;
    // The inverse service-ID map is not implemented; fail closed.
    WSASetLastError(WSAEOPNOTSUPP);
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
    DWORD status = 0x00000001u;
    if (adapter->TransmitLinkSpeed >= 100000000ull) status |= 0x00000002u;
    if (adapter->IfType == IF_TYPE_IEEE80211) status |= 0x00000020u;
    return status;
}

extern "C" DWORD WINAPI XNetGetTitleXnAddr(XNADDR* output) {
    if (!output) return 0;
    std::memset(output, 0, sizeof(*output));
    std::vector<BYTE> storage;
    auto* adapter = FindUsableIPv4Adapter(storage);
    if (!adapter) return 0;
    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
        if (unicast->Address.lpSockaddr && unicast->Address.lpSockaddr->sa_family == AF_INET) {
            const auto* ip = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
            if (ip->sin_addr.s_addr != htonl(INADDR_ANY) && ip->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) { output->ina = ip->sin_addr; break; }
        }
    }
    if (adapter->PhysicalAddressLength >= sizeof(output->abEnet)) std::memcpy(output->abEnet, adapter->PhysicalAddress, sizeof(output->abEnet));
    DWORD flags = adapter->Dhcpv4Enabled ? 0x00000002u : 0x00000001u;
    if (adapter->FirstGatewayAddress) flags |= 0x00000020u;
    if (adapter->FirstDnsServerAddress) flags |= 0x00000040u;
    return flags;
}


// XSocket handles are guest 32-bit tokens. They are deliberately not host
// SOCKET values, because the host runtime may be a separate 64-bit process.
extern "C" SOCKET WINAPI XSocketCreate(int af, int type, int protocol) {
    if (af != AF_INET) return SocketHandleFailure(WSAEAFNOSUPPORT);
    if (type == SOCK_STREAM) {
        if (protocol != 0 && protocol != IPPROTO_TCP) return SocketHandleFailure(WSAEPROTONOSUPPORT);
        const uint32_t handle = XboxNetSocketCreate(1);
        return handle ? static_cast<SOCKET>(handle) : INVALID_SOCKET;
    }
    if (type == SOCK_DGRAM) {
        if (protocol != 0 && protocol != IPPROTO_UDP) return SocketHandleFailure(WSAEPROTONOSUPPORT);
        const uint32_t handle = XboxNetSocketCreate(2);
        return handle ? static_cast<SOCKET>(handle) : INVALID_SOCKET;
    }
    return SocketHandleFailure(WSAESOCKTNOSUPPORT);
}

extern "C" int WINAPI XWSAStartup(WORD requestedVersion, LPWSADATA output) {
    if (!output) return WSAEFAULT;
    return XboxNetStartupWithData(requestedVersion, output);
}

extern "C" int WINAPI XWSACleanup() {
    return XboxNetCleanup();
}

extern "C" int WINAPI XSocketWSAGetLastError() {
    return WSAGetLastError();
}

extern "C" void WINAPI XWSASetLastError(int error) {
    WSASetLastError(error);
}

extern "C" WSAEVENT WINAPI XWSACreateEvent() {
    return WSACreateEvent();
}

extern "C" BOOL WINAPI XWSACloseEvent(WSAEVENT eventHandle) {
    if (!eventHandle) { WSASetLastError(WSAEINVAL); return FALSE; }
    return WSACloseEvent(eventHandle);
}

extern "C" BOOL WINAPI XWSASetEvent(WSAEVENT eventHandle) {
    if (!eventHandle) { WSASetLastError(WSAEINVAL); return FALSE; }
    return WSASetEvent(eventHandle);
}

extern "C" BOOL WINAPI XWSAResetEvent(WSAEVENT eventHandle) {
    if (!eventHandle) { WSASetLastError(WSAEINVAL); return FALSE; }
    return WSAResetEvent(eventHandle);
}

extern "C" DWORD WINAPI XWSAWaitForMultipleEvents(DWORD count,
    const WSAEVENT* events, BOOL waitAll, DWORD timeout, BOOL alertable) {
    if (!count || !events) { WSASetLastError(WSAEINVAL); return WSA_WAIT_FAILED; }
    return WSAWaitForMultipleEvents(count, events, waitAll, timeout, alertable);
}

extern "C" BOOL WINAPI XWSAFDIsSet(SOCKET socket, fd_set* set) {
    if (!set) { WSASetLastError(WSAEFAULT); return FALSE; }
    if (set->fd_count > FD_SETSIZE) { WSASetLastError(WSAEINVAL); return FALSE; }
    for (u_int i = 0; i < set->fd_count; ++i) {
        if (set->fd_array[i] == socket) return TRUE;
    }
    return FALSE;
}

extern "C" int WINAPI XWSAEventSelect(SOCKET socket, WSAEVENT eventHandle,
    long networkEvents) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    if (!eventHandle) return SocketFailure(WSAEINVAL);
    return XboxNetSocketEventSelect(static_cast<uint32_t>(socket),
        eventHandle, networkEvents);
}
extern "C" WSAEVENT WINAPI XSocketWSACreateEvent() { return XWSACreateEvent(); }
extern "C" BOOL WINAPI XSocketWSACloseEvent(WSAEVENT e) { return XWSACloseEvent(e); }
extern "C" BOOL WINAPI XSocketWSASetEvent(WSAEVENT e) { return XWSASetEvent(e); }
extern "C" BOOL WINAPI XSocketWSAResetEvent(WSAEVENT e) { return XWSAResetEvent(e); }
extern "C" DWORD WINAPI XSocketWSAWaitForMultipleEvents(DWORD n, const WSAEVENT* e, BOOL all, DWORD ms, BOOL alertable) { return XWSAWaitForMultipleEvents(n,e,all,ms,alertable); }
extern "C" BOOL WINAPI XSocketWSAFDIsSet(SOCKET s, fd_set* set) { return XWSAFDIsSet(s,set); }
extern "C" int WINAPI XSocketWSAEventSelect(SOCKET s, WSAEVENT e, long events) { return XWSAEventSelect(s,e,events); }
extern "C" void WINAPI XSocketWSASetLastError(int error) { XWSASetLastError(error); }

extern "C" int WINAPI XSocketSelect(int, fd_set* readSet, fd_set* writeSet,
    fd_set* exceptSet, const timeval* timeout) {
    return XboxNetSocketSelect(readSet, writeSet, exceptSet, timeout);
}

extern "C" int WINAPI XSocketGetLastError() {
    return WSAGetLastError();
}

extern "C" void WINAPI XSocketSetLastError(int error) {
    WSASetLastError(error);
}

extern "C" SOCKET WINAPI XCreateSocket(int af, int type, int protocol) { return XSocketCreate(af, type, protocol); }
extern "C" BOOL WINAPI XSocketWSAGetOverlappedResult(SOCKET socket, LPWSAOVERLAPPED ov, LPDWORD transferred, BOOL wait, LPDWORD flags) {
    if (socket == INVALID_SOCKET || socket == 0) { WSASetLastError(WSAENOTSOCK); return FALSE; }
    return XboxNetSocketWSAGetOverlappedResult(static_cast<uint32_t>(socket), ov, transferred, wait, flags);
}
extern "C" int WINAPI XSocketWSACancelOverlappedIO(SOCKET socket) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketWSACancelOverlappedIO(static_cast<uint32_t>(socket));
}
extern "C" int WINAPI XSocketWSARecv(SOCKET socket, LPWSABUF b, DWORD n, LPDWORD bytes, LPDWORD flags, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketWSARecv(static_cast<uint32_t>(socket), b, n, bytes, flags, ov, cb);
}
extern "C" int WINAPI XSocketWSARecvFrom(SOCKET socket, LPWSABUF b, DWORD n, LPDWORD bytes, LPDWORD flags, sockaddr* from, LPINT fromLen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketWSARecvFrom(static_cast<uint32_t>(socket), b, n, bytes, flags, from, fromLen, ov, cb);
}
extern "C" int WINAPI XSocketWSASend(SOCKET socket, LPWSABUF b, DWORD n, LPDWORD bytes, DWORD flags, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketWSASend(static_cast<uint32_t>(socket), b, n, bytes, flags, ov, cb);
}
extern "C" int WINAPI XSocketWSASendTo(SOCKET socket, LPWSABUF b, DWORD n, LPDWORD bytes, DWORD flags, const sockaddr* to, int toLen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketWSASendTo(static_cast<uint32_t>(socket), b, n, bytes, flags, to, toLen, ov, cb);
}

extern "C" int WINAPI XSocketClose(SOCKET socket) {
    if (socket == INVALID_SOCKET || socket == 0) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketClose(static_cast<uint32_t>(socket)) == 0 ? 0 : SOCKET_ERROR;
}

extern "C" int WINAPI XSocketBind(SOCKET socket, const sockaddr* address, int addressLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!address) return SocketFailure(WSAEFAULT);
    if (addressLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
    if (address->sa_family != AF_INET) return SocketFailure(WSAEAFNOSUPPORT);
    return XboxNetSocketBindAddress(static_cast<uint32_t>(socket), address,
        static_cast<uint32_t>(addressLength));
}

extern "C" int WINAPI XSocketConnect(SOCKET socket, const sockaddr* address, int addressLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!address) return SocketFailure(WSAEFAULT);
    if (addressLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
    if (address->sa_family != AF_INET) return SocketFailure(WSAEAFNOSUPPORT);
    return XboxNetSocketConnectAddress(static_cast<uint32_t>(socket), address,
        static_cast<uint32_t>(addressLength));
}

extern "C" int WINAPI XSocketListen(SOCKET socket, int backlog) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketListen(static_cast<uint32_t>(socket), backlog);
}

extern "C" SOCKET WINAPI XSocketAccept(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET) return SocketHandleFailure(WSAENOTSOCK);
    if ((address && !addressLength) || (!address && addressLength)) return SocketHandleFailure(WSAEFAULT);
    if (addressLength && *addressLength < static_cast<int>(sizeof(sockaddr_in))) return SocketHandleFailure(WSAEINVAL);
    uint32_t length = addressLength && *addressLength > 0
        ? static_cast<uint32_t>(*addressLength) : 0;
    const uint32_t accepted = XboxNetSocketAccept(static_cast<uint32_t>(socket),
        address, addressLength ? &length : nullptr);
    if (!accepted) return INVALID_SOCKET;
    if (addressLength && address) *addressLength = static_cast<int>(length);
    return static_cast<SOCKET>(accepted);
}

extern "C" int WINAPI XSocketShutdown(SOCKET socket, int how) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    return XboxNetSocketShutdown(static_cast<uint32_t>(socket), how);
}

extern "C" int WINAPI XSocketIOCTLSocket(SOCKET socket, long command, u_long* argument) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!argument) return SocketFailure(WSAEFAULT);
    if (command != FIONBIO) return SocketFailure(WSAEINVAL);
    return XboxNetSocketSetNonBlocking(static_cast<uint32_t>(socket), *argument != 0);
}

extern "C" int WINAPI XSocketSetSockOpt(SOCKET socket, int level, int option, const char* value, int length) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!value && length) return SocketFailure(WSAEFAULT);
    if (length < 0) return SocketFailure(WSAEINVAL);
    return XboxNetSocketSetOption(static_cast<uint32_t>(socket), level, option, value, length);
}

extern "C" int WINAPI XSocketGetSockOpt(SOCKET socket, int level, int option, char* value, int* length) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!value || !length) return SocketFailure(WSAEFAULT);
    return XboxNetSocketGetOption(static_cast<uint32_t>(socket), level, option, value, length);
}

extern "C" int WINAPI XSocketGetSockName(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!address || !addressLength) return SocketFailure(WSAEFAULT);
    if (*addressLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
    uint32_t length = static_cast<uint32_t>(*addressLength);
    const int result = XboxNetSocketGetName(static_cast<uint32_t>(socket), address, &length);
    if (result == 0) *addressLength = static_cast<int>(length);
    return result;
}

extern "C" int WINAPI XSocketGetPeerName(SOCKET socket, sockaddr* address, int* addressLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (!address || !addressLength) return SocketFailure(WSAEFAULT);
    if (*addressLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
    uint32_t length = static_cast<uint32_t>(*addressLength);
    const int result = XboxNetSocketGetPeerName(static_cast<uint32_t>(socket), address, &length);
    if (result == 0) *addressLength = static_cast<int>(length);
    return result;
}

extern "C" int WINAPI XSocketSend(SOCKET socket, const char* buffer, int length, int flags) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (length < 0) return SocketFailure(WSAEINVAL);
    if (!buffer && length) return SocketFailure(WSAEFAULT);
    return XboxNetSocketSend(static_cast<uint32_t>(socket), buffer, static_cast<uint32_t>(length), flags);
}

extern "C" int WINAPI XSocketRecv(SOCKET socket, char* buffer, int length, int flags) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (length < 0) return SocketFailure(WSAEINVAL);
    if (!buffer && length) return SocketFailure(WSAEFAULT);
    return XboxNetSocketReceive(static_cast<uint32_t>(socket), buffer, static_cast<uint32_t>(length), flags);
}

extern "C" int WINAPI XSocketSendTo(SOCKET socket, const char* buffer, int length, int flags,
    const sockaddr* to, int toLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (length < 0) return SocketFailure(WSAEINVAL);
    if ((!buffer && length) || !to) return SocketFailure(WSAEFAULT);
    if (toLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
    if (to->sa_family != AF_INET) return SocketFailure(WSAEAFNOSUPPORT);
    const sockaddr_in* target = reinterpret_cast<const sockaddr_in*>(to);
    return XboxNetSocketSendTo(static_cast<uint32_t>(socket), buffer,
        static_cast<uint32_t>(length), target->sin_addr.s_addr, ntohs(target->sin_port), flags);
}

extern "C" int WINAPI XSocketRecvFrom(SOCKET socket, char* buffer, int length, int flags,
    sockaddr* from, int* fromLength) {
    if (socket == INVALID_SOCKET) return SocketFailure(WSAENOTSOCK);
    if (length < 0) return SocketFailure(WSAEINVAL);
    if (!buffer && length) return SocketFailure(WSAEFAULT);
    if (from && !fromLength) return SocketFailure(WSAEFAULT);
    if (from && *fromLength < static_cast<int>(sizeof(sockaddr_in))) return SocketFailure(WSAEINVAL);
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

extern "C" u_long WINAPI XSocketInet_Addr(const char* text) {
    if (!text) {
        WSASetLastError(WSAEFAULT);
        return INADDR_NONE;
    }
    IN_ADDR address{};
    if (InetPtonA(AF_INET, text, &address) != 1) {
        WSASetLastError(WSAEINVAL);
        return INADDR_NONE;
    }
    return address.s_addr;
}

extern "C" u_long WINAPI XSocketHTONL(u_long value) { return htonl(value); }
extern "C" u_long WINAPI XSocketNTOHL(u_long value) { return ntohl(value); }
extern "C" u_short WINAPI XSocketHTONS(u_short value) { return htons(value); }
extern "C" u_short WINAPI XSocketNTOHS(u_short value) { return ntohs(value); }
