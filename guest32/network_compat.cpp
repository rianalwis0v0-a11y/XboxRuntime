#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <climits>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <string>
#include <limits>
#include "network_compat.h"

namespace {
std::mutex g_mutex;
std::unordered_map<uint32_t, SOCKET> g_sockets;
uint32_t g_nextHandle = 1;
unsigned g_startUsers = 0;

SOCKET FindSocket(uint32_t token) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_sockets.find(token);
    return it == g_sockets.end() ? INVALID_SOCKET : it->second;
}
uint32_t StoreSocket(SOCKET socket) {
    if (socket == INVALID_SOCKET) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (uint64_t tries = 0; tries < UINT32_MAX - 1ull; ++tries) {
        uint32_t candidate = g_nextHandle++;
        if (!candidate) candidate = g_nextHandle++;
        if (g_sockets.find(candidate) == g_sockets.end()) {
            g_sockets.emplace(candidate, socket);
            return candidate;
        }
    }
    closesocket(socket);
    return 0;
}
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
    for (auto& entry : g_sockets) closesocket(entry.second);
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
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET) return -1;
    sockaddr_in address{};
    int type = 0, size = sizeof(type);
    if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &size) == SOCKET_ERROR ||
        !Resolve(hostname, port, type, address)) return -1;
    return ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 ? 0 : -1;
}

extern "C" int __cdecl XboxNetSocketBind(uint32_t handle, uint16_t port) {
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    return ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 ? 0 : -1;
}

extern "C" int __cdecl XboxNetSocketListen(uint32_t handle, int backlog) {
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET || backlog < 0) return -1;
    return ::listen(socket, backlog) == 0 ? 0 : -1;
}

extern "C" uint32_t __cdecl XboxNetSocketAccept(uint32_t handle) {
    SOCKET listener = FindSocket(handle);
    if (listener == INVALID_SOCKET) return 0;
    SOCKET accepted = ::accept(listener, nullptr, nullptr);
    return StoreSocket(accepted);
}

extern "C" int32_t __cdecl XboxNetSocketSend(uint32_t handle, const void* data, uint32_t length) {
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET || (!data && length) || length > INT_MAX) return -1;
    return ::send(socket, static_cast<const char*>(data), static_cast<int>(length), 0);
}

extern "C" int32_t __cdecl XboxNetSocketReceive(uint32_t handle, void* data, uint32_t capacity) {
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET || (!data && capacity) || capacity > INT_MAX) return -1;
    return ::recv(socket, static_cast<char*>(data), static_cast<int>(capacity), 0);
}

extern "C" int __cdecl XboxNetSocketSetTimeout(uint32_t handle, uint32_t milliseconds) {
    SOCKET socket = FindSocket(handle);
    if (socket == INVALID_SOCKET) return -1;
    DWORD timeout = milliseconds;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 &&
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0 ? 0 : -1;
}

extern "C" void __cdecl XboxNetSocketClose(uint32_t handle) {
    SOCKET socket = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sockets.find(handle);
        if (it == g_sockets.end()) return;
        socket = it->second;
        g_sockets.erase(it);
    }
    closesocket(socket);
}
