#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <climits>
#include <cstring>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <string>
#include <limits>
#include <vector>
#include <utility>
#include "network_compat.h"

namespace {
std::mutex g_mutex;
struct SocketState {
    explicit SocketState(SOCKET value) : socket(value) {}
    ~SocketState() { if (socket != INVALID_SOCKET) closesocket(socket); }
    SOCKET socket;
};
struct SocketLease {
    std::shared_ptr<SocketState> state;
    operator SOCKET() const { return state ? state->socket : INVALID_SOCKET; }
};
std::unordered_map<uint32_t, std::shared_ptr<SocketState>> g_sockets;
uint32_t g_nextHandle = 1;
unsigned g_startUsers = 0;

SocketLease FindSocket(uint32_t token) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_sockets.find(token);
    return { it == g_sockets.end() ? std::shared_ptr<SocketState>{} : it->second };
}
uint32_t StoreSocket(SOCKET socket) {
    if (socket == INVALID_SOCKET) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (uint64_t tries = 0; tries < UINT32_MAX - 1ull; ++tries) {
        uint32_t candidate = g_nextHandle++;
        if (!candidate) candidate = g_nextHandle++;
        if (g_sockets.find(candidate) == g_sockets.end()) {
            try {
                g_sockets.emplace(candidate, std::make_shared<SocketState>(socket));
            } catch (...) {
                closesocket(socket);
                return 0;
            }
            return candidate;
        }
    }
    closesocket(socket);
    return 0;
}
// DNS is delegated to the Windows resolver so the guest inherits the host's
// configured DNS servers, search suffixes and policy. Service-specific DNS
// (including Insignia) belongs to the separately configured service layer.
bool Resolve(const char* hostname, uint16_t port, int type, sockaddr_in& address) {
    if (!hostname || !*hostname) return false;
    ADDRINFOA hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = type;
    hints.ai_protocol = type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    ADDRINFOA* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 || !result) return false;
    address = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
    address.sin_port = htons(port);
    freeaddrinfo(result);
    return true;
}

extern "C" int __cdecl XboxNetStartup() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_startUsers) { ++g_startUsers; return 0; }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return -1;
    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
        WSACleanup();
        return -1;
    }
    g_startUsers = 1;
    return 0;
}

extern "C" void __cdecl XboxNetCleanup() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_startUsers || --g_startUsers) return;
    // Active operations hold SocketLease references. Clearing the registry
    // removes guest-visible handles; each native socket closes after its last
    // in-flight operation releases its lease.
    g_sockets.clear();
    WSACleanup();
}

extern "C" int32_t __cdecl XboxNetResolveIPv4(const char* hostname, uint32_t* output) {
    if (!hostname || !output) return -1;
    sockaddr_in address{};
    if (!Resolve(hostname, 0, SOCK_DGRAM, address)) return -1;
    *output = address.sin_addr.s_addr;
    return 0;
}

extern "C" uint32_t __cdecl XboxNetSocketCreate(int type) {
    if (type != 1 && type != 2) return 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_startUsers) return 0;
    }
    SOCKET socket = ::socket(AF_INET, type == 1 ? SOCK_STREAM : SOCK_DGRAM,
        type == 1 ? IPPROTO_TCP : IPPROTO_UDP);
    return StoreSocket(socket);
}

extern "C" int __cdecl XboxNetSocketConnect(uint32_t handle, const char* hostname, uint16_t port) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET) return -1;
    sockaddr_in address{};
    int type = 0, size = sizeof(type);
    if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) == SOCKET_ERROR ||
        !Resolve(hostname, port, type, address)) return -1;
    return ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 ? 0 : -1;
}

extern "C" int __cdecl XboxNetSocketConnectAddress(uint32_t handle, const void* rawAddress, uint32_t length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || !rawAddress || length < sizeof(sockaddr_in)) return -1;
    return ::connect(socket, static_cast<const sockaddr*>(rawAddress), static_cast<int>(length));
}

extern "C" int __cdecl XboxNetSocketBindAddress(uint32_t handle, const void* rawAddress, uint32_t length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || !rawAddress || length < sizeof(sockaddr_in)) return -1;
    return ::bind(socket, static_cast<const sockaddr*>(rawAddress), static_cast<int>(length));
}

extern "C" int __cdecl XboxNetSocketShutdown(uint32_t handle, int how) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET) return -1;
    return ::shutdown(socket, how);
}

extern "C" int __cdecl XboxNetSocketGetName(uint32_t handle, void* rawAddress, uint32_t* length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || !rawAddress || !length || *length < sizeof(sockaddr_in)) return -1;
    int nativeLength = sizeof(sockaddr_in);
    const int result = getsockname(socket, static_cast<sockaddr*>(rawAddress), &nativeLength);
    if (result == 0) *length = static_cast<uint32_t>(nativeLength);
    return result;
}

extern "C" int __cdecl XboxNetSocketGetPeerName(uint32_t handle, void* rawAddress, uint32_t* length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || !rawAddress || !length || *length < sizeof(sockaddr_in)) return -1;
    int nativeLength = sizeof(sockaddr_in);
    const int result = getpeername(socket, static_cast<sockaddr*>(rawAddress), &nativeLength);
    if (result == 0) *length = static_cast<uint32_t>(nativeLength);
    return result;
}

extern "C" int __cdecl XboxNetSocketSetOption(uint32_t handle, int level, int option, const char* value, int length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || (!value && length) || length < 0) return -1;
    return setsockopt(socket, level, option, value, length);
}

extern "C" int __cdecl XboxNetSocketGetOption(uint32_t handle, int level, int option, char* value, int* length) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || !value || !length) return -1;
    return getsockopt(socket, level, option, value, length);
}

extern "C" int __cdecl XboxNetSocketBind(uint32_t handle, uint16_t port) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    return ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 ? 0 : -1;
}

extern "C" int __cdecl XboxNetSocketListen(uint32_t handle, int backlog) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || backlog < 0) return -1;
    return ::listen(socket, backlog) == 0 ? 0 : -1;
}

extern "C" uint32_t __cdecl XboxNetSocketAccept(uint32_t handle, void* rawAddress, uint32_t* addressLength) {
    SocketLease listenerLease = FindSocket(handle);
    SOCKET listener = listenerLease;
    if (listener == INVALID_SOCKET) return 0;
    sockaddr_in peer{};
    int peerLength = sizeof(peer);
    SOCKET accepted = ::accept(listener,
        rawAddress ? reinterpret_cast<sockaddr*>(&peer) : nullptr,
        rawAddress ? &peerLength : nullptr);
    if (accepted == INVALID_SOCKET) return 0;
    if (rawAddress && addressLength) {
        if (*addressLength < sizeof(peer)) {
            closesocket(accepted);
            return 0;
        }
        std::memcpy(rawAddress, &peer, sizeof(peer));
        *addressLength = sizeof(peer);
    }
    return StoreSocket(accepted);
}

extern "C" int32_t __cdecl XboxNetSocketSend(uint32_t handle, const void* data, uint32_t length, int flags) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || (!data && length) || length > INT_MAX) return -1;
    return ::send(socket, static_cast<const char*>(data), static_cast<int>(length), flags);
}

extern "C" int32_t __cdecl XboxNetSocketReceive(uint32_t handle, void* data, uint32_t capacity, int flags) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || (!data && capacity) || capacity > INT_MAX) return -1;
    return ::recv(socket, static_cast<char*>(data), static_cast<int>(capacity), flags);
}

extern "C" int32_t __cdecl XboxNetSocketSendTo(uint32_t handle, const void* data, uint32_t length, uint32_t ipv4, uint16_t port, int flags) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || (!data && length) || length > INT_MAX) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ipv4;
    address.sin_port = htons(port);
    return ::sendto(socket, static_cast<const char*>(data), static_cast<int>(length), flags,
        reinterpret_cast<sockaddr*>(&address), sizeof(address));
}

extern "C" int32_t __cdecl XboxNetSocketReceiveFrom(uint32_t handle, void* data, uint32_t capacity, uint32_t* ipv4, uint16_t* port, int flags) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET || (!data && capacity) || capacity > INT_MAX) return -1;
    sockaddr_in address{};
    int addressLength = sizeof(address);
    int received = ::recvfrom(socket, static_cast<char*>(data), static_cast<int>(capacity), flags,
        reinterpret_cast<sockaddr*>(&address), &addressLength);
    if (received >= 0) {
        if (ipv4) *ipv4 = address.sin_addr.s_addr;
        if (port) *port = ntohs(address.sin_port);
    }
    return received;
}

extern "C" int __cdecl XboxNetSocketSetNonBlocking(uint32_t handle, int enabled) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET) return -1;
    u_long mode = enabled ? 1ul : 0ul;
    return ioctlsocket(socket, FIONBIO, &mode) == 0 ? 0 : -1;
}


extern "C" int __cdecl XboxNetSocketSelect(int, fd_set* readfds, fd_set* writefds,
    fd_set* exceptfds, const timeval* timeout) {
    // Xbox fd_set entries are guest tokens. Translate them to native handles
    // for select(), then translate the ready sets back to guest tokens.
    if (!readfds && !writefds && !exceptfds) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    if (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
        timeout->tv_usec >= 1000000)) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    fd_set nativeRead{}, nativeWrite{}, nativeExcept{};
    fd_set* nativeReadPtr = readfds ? &nativeRead : nullptr;
    fd_set* nativeWritePtr = writefds ? &nativeWrite : nullptr;
    fd_set* nativeExceptPtr = exceptfds ? &nativeExcept : nullptr;
    std::vector<std::pair<uint32_t, SocketLease>> leases;
    std::vector<uint32_t> readTokens, writeTokens, exceptTokens;

    try {
        auto translate = [&](fd_set* guestSet, fd_set* nativeSet,
            std::vector<uint32_t>& tokens) -> bool {
            if (!guestSet) return true;
            if (guestSet->fd_count > FD_SETSIZE) {
                WSASetLastError(WSAEINVAL);
                return false;
            }
            tokens.reserve(guestSet->fd_count);
            for (u_int i = 0; i < guestSet->fd_count; ++i) {
                const uint32_t token = static_cast<uint32_t>(guestSet->fd_array[i]);
                SocketLease lease = FindSocket(token);
                const SOCKET nativeSocket = lease;
                if (nativeSocket == INVALID_SOCKET) {
                    WSASetLastError(WSAENOTSOCK);
                    return false;
                }
                tokens.push_back(token);
                FD_SET(nativeSocket, nativeSet);
                leases.emplace_back(token, std::move(lease));
            }
            return true;
        };

        if (!translate(readfds, nativeReadPtr, readTokens) ||
            !translate(writefds, nativeWritePtr, writeTokens) ||
            !translate(exceptfds, nativeExceptPtr, exceptTokens))
            return SOCKET_ERROR;
    } catch (...) {
        WSASetLastError(WSAENOBUFS);
        return SOCKET_ERROR;
    }

    timeval timeoutCopy{};
    timeval* timeoutPtr = nullptr;
    if (timeout) {
        timeoutCopy = *timeout;
        timeoutPtr = &timeoutCopy;
    }

    const int result = ::select(0, nativeReadPtr, nativeWritePtr,
        nativeExceptPtr, timeoutPtr);
    if (result == SOCKET_ERROR) return SOCKET_ERROR;

    auto restore = [&](fd_set* guestSet, const fd_set* nativeSet,
        const std::vector<uint32_t>& tokens) {
        if (!guestSet) return;
        FD_ZERO(guestSet);
        for (uint32_t token : tokens) {
            for (const auto& entry : leases) {
                if (entry.first == token && FD_ISSET(static_cast<SOCKET>(entry.second), nativeSet)) {
                    FD_SET(static_cast<SOCKET>(token), guestSet);
                    break;
                }
            }
        }
    };
    if (readfds) restore(readfds, &nativeRead, readTokens);
    if (writefds) restore(writefds, &nativeWrite, writeTokens);
    if (exceptfds) restore(exceptfds, &nativeExcept, exceptTokens);
    return result;
}

extern "C" int __cdecl XboxNetSocketSetTimeout(uint32_t handle, uint32_t milliseconds) {
    SocketLease socketLease = FindSocket(handle);
    SOCKET socket = socketLease;
    if (socket == INVALID_SOCKET) return -1;
    DWORD timeout = milliseconds;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 ? 0 : -1;
}

extern "C" int __cdecl XboxNetSocketClose(uint32_t handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return -1;
    // Erasing the registry entry invalidates the guest handle immediately.
    // In-flight operations keep a shared lease until they safely complete.
    g_sockets.erase(it);
    return 0;
}
