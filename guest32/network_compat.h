#pragma once
#include <cstdint>
#include <winsock2.h>

// Internal guest32 networking bridge. Handles are 32-bit guest tokens, never
// raw Winsock SOCKET values. All functions use the x86 guest calling ABI.
extern "C" {
int __cdecl XboxNetStartup();
void __cdecl XboxNetCleanup();
int32_t __cdecl XboxNetResolveIPv4(const char* hostname, uint32_t* addressNetworkOrder);
uint32_t __cdecl XboxNetSocketCreate(int type); // 1 = stream/TCP, 2 = datagram/UDP
int __cdecl XboxNetSocketConnect(uint32_t handle, const char* hostname, uint16_t port);
int __cdecl XboxNetSocketConnectAddress(uint32_t handle, const void* sockaddrIn, uint32_t length);
int __cdecl XboxNetSocketBindAddress(uint32_t handle, const void* sockaddrIn, uint32_t length);
int __cdecl XboxNetSocketShutdown(uint32_t handle, int how);
int __cdecl XboxNetSocketGetName(uint32_t handle, void* address, uint32_t* length);
int __cdecl XboxNetSocketGetPeerName(uint32_t handle, void* address, uint32_t* length);
int __cdecl XboxNetSocketSetOption(uint32_t handle, int level, int option, const char* value, int length);
int __cdecl XboxNetSocketGetOption(uint32_t handle, int level, int option, char* value, int* length);
int __cdecl XboxNetSocketBind(uint32_t handle, uint16_t port);
int __cdecl XboxNetSocketListen(uint32_t handle, int backlog);
uint32_t __cdecl XboxNetSocketAccept(uint32_t handle, void* address, uint32_t* addressLength);
int32_t __cdecl XboxNetSocketSend(uint32_t handle, const void* data, uint32_t length, int flags);
int32_t __cdecl XboxNetSocketReceive(uint32_t handle, void* data, uint32_t capacity, int flags);
// IPv4 address is network byte order; port is host byte order.
int32_t __cdecl XboxNetSocketSendTo(uint32_t handle, const void* data, uint32_t length, uint32_t ipv4NetworkOrder, uint16_t port, int flags);
int32_t __cdecl XboxNetSocketReceiveFrom(uint32_t handle, void* data, uint32_t capacity, uint32_t* ipv4NetworkOrder, uint16_t* port, int flags);
int __cdecl XboxNetSocketSetNonBlocking(uint32_t handle, int enabled);
// fd_set values contain guest socket tokens, not native SOCKET values.
int __cdecl XboxNetSocketSelect(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout);
int __cdecl XboxNetSocketSetTimeout(uint32_t handle, uint32_t milliseconds);
int __cdecl XboxNetSocketClose(uint32_t handle);
}
