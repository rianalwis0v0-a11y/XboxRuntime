#pragma once
#include <cstdint>
#include <winsock2.h>

// Internal guest32 networking bridge. Handles are 32-bit guest tokens, never
// raw Winsock SOCKET values. All functions use the x86 guest calling ABI.
extern "C" {
int __cdecl XboxNetStartup();
int __cdecl XboxNetStartupWithData(WORD requestedVersion, WSADATA* output);
int __cdecl XboxNetCleanup();
void __cdecl XboxNetSetSocketLimits(unsigned maxDatagramSockets, unsigned maxStreamSockets);
void __cdecl XboxNetSetSocketDefaults(unsigned receiveBufferKilobytes, unsigned sendBufferKilobytes);
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
int __cdecl XboxNetSocketSetTimeout(uint32_t handle, uint32_t milliseconds);
int __cdecl XboxNetSocketSelect(fd_set* readSet, fd_set* writeSet, fd_set* exceptSet, const timeval* timeout);
int __cdecl XboxNetSocketEventSelect(uint32_t handle, WSAEVENT eventHandle, long networkEvents);
int __cdecl XboxNetSocketWSARecv(uint32_t handle, LPWSABUF buffers, DWORD count, LPDWORD bytes, LPDWORD flags, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int __cdecl XboxNetSocketWSARecvFrom(uint32_t handle, LPWSABUF buffers, DWORD count, LPDWORD bytes, LPDWORD flags, sockaddr* from, LPINT fromLength, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int __cdecl XboxNetSocketWSASend(uint32_t handle, LPWSABUF buffers, DWORD count, LPDWORD bytes, DWORD flags, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
int __cdecl XboxNetSocketWSASendTo(uint32_t handle, LPWSABUF buffers, DWORD count, LPDWORD bytes, DWORD flags, const sockaddr* to, int toLength, LPWSAOVERLAPPED overlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE completion);
BOOL __cdecl XboxNetSocketWSAGetOverlappedResult(uint32_t handle, LPWSAOVERLAPPED overlapped, LPDWORD transferred, BOOL wait, LPDWORD flags);
int __cdecl XboxNetSocketWSACancelOverlappedIO(uint32_t handle);
int __cdecl XboxNetSocketClose(uint32_t handle);
}
