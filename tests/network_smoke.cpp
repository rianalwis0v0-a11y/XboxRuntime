#include <cstdint>
#include <cstdio>
#include <cstring>
#include "network_compat.h"
#include "xnet_compat.h"

int main() {
    XNetStartupParams params{};
    params.cfgSizeOfStruct = sizeof(params);
    params.cfgKeyRegMax = 1;
    if (XNetStartup(&params) != 0) return 1;

    WSADATA startupData{};
    if (XWSAStartup(MAKEWORD(2, 2), &startupData) != 0 ||
        startupData.wVersion != MAKEWORD(2, 2)) {
        XNetCleanup();
        return 28;
    }
    WSAEVENT eventProbe = XWSACreateEvent();
    if (!eventProbe || !XWSASetEvent(eventProbe) ||
        XWSAWaitForMultipleEvents(1, &eventProbe, TRUE, 0, FALSE) != WSA_WAIT_EVENT_0 ||
        !XWSAResetEvent(eventProbe) ||
        XWSAWaitForMultipleEvents(1, &eventProbe, TRUE, 0, FALSE) != WSA_WAIT_TIMEOUT ||
        !XWSACloseEvent(eventProbe)) {
        if (eventProbe) XWSACloseEvent(eventProbe);
        XWSACleanup();
        XNetCleanup();
        return 29;
    }

    BYTE randomBytes[32]{};
    if (XNetRandom(nullptr, sizeof(randomBytes)) != WSAEFAULT ||
        XNetRandom(randomBytes, sizeof(randomBytes)) != 0)
        return 11;
    bool randomHasData = false;
    for (BYTE value : randomBytes) randomHasData = randomHasData || value != 0;
    if (!randomHasData) return 12;

    XNKID firstId{}, secondId{};
    XNKEY firstKey{}, secondKey{};
    if (XNetCreateKey(&firstId, &firstKey) != 0 ||
        XNetCreateKey(&secondId, &secondKey) != 0 ||
        (firstId.ab[0] & 0xF0u) != 0 ||
        XNetRegisterKey(&firstId, &firstKey) != 0 ||
        XNetRegisterKey(&firstId, &firstKey) != WSAEALREADY ||
        XNetRegisterKey(&secondId, &secondKey) != WSAENOMORE ||
        XNetUnregisterKey(&firstId) != 0 ||
        XNetUnregisterKey(&firstId) != WSAEINVAL)
        return 13;

    WSAEVENT dnsEvent = WSACreateEvent();
    XNDNS* dns = nullptr;
    if (!dnsEvent || XNetDnsLookup("localhost", dnsEvent, &dns) != 0 ||
        !dns || dns->iStatus != WSAEINPROGRESS ||
        WSAWaitForMultipleEvents(1, &dnsEvent, TRUE, 5000, FALSE) != WSA_WAIT_EVENT_0 ||
        dns->iStatus != 0 || dns->cina == 0) {
        if (dns) XNetDnsRelease(dns);
        if (dnsEvent) WSACloseEvent(dnsEvent);
        XNetCleanup();
        return 1;
    }
    XNetDnsRelease(dns);
    WSACloseEvent(dnsEvent);

    IN_ADDR unmappedAddress{}, mappedAddress{};
    unmappedAddress.s_addr = htonl(INADDR_LOOPBACK);
    if (XNetServerToInAddr(unmappedAddress, 1, &mappedAddress) != WSAEOPNOTSUPP ||
        WSAGetLastError() != WSAEOPNOTSUPP ||
        XNetInAddrToServer(unmappedAddress, &mappedAddress) != WSAEOPNOTSUPP ||
        WSAGetLastError() != WSAEOPNOTSUPP) {
        XNetCleanup();
        return 27;
    }

    if (XSocketInet_Addr("127.0.0.1") != htonl(INADDR_LOOPBACK) ||
        XSocketInet_Addr("not-an-ip") != INADDR_NONE ||
        WSAGetLastError() != WSAEINVAL) {
        XNetCleanup();
        return 33;
    }

    char addressText[16]{};
    IN_ADDR loopbackAddress{};
    loopbackAddress.s_addr = htonl(INADDR_LOOPBACK);
    if (XNetInAddrToString(loopbackAddress, addressText, sizeof(addressText)) != 0 ||
        std::strcmp(addressText, "127.0.0.1") != 0 ||
        XNetInAddrToString(loopbackAddress, addressText, 2) == 0) {
        XNetCleanup();
        return 23;
    }

    uint32_t loopback = 0;
    if (XboxNetResolveIPv4("localhost", &loopback) != 0 || loopback == 0) {
        XNetCleanup();
        return 2;
    }
    // Invalid guest tokens must report a useful Winsock error, not just -1.
    if (XboxNetSocketSend(0xFFFFFFFFu, "x", 1, 0) != SOCKET_ERROR ||
        WSAGetLastError() != WSAENOTSOCK) {
        XNetCleanup();
        return 14;
    }
    if (XSocketSend(static_cast<SOCKET>(0xFFFFFFFEu), "x", 1, 0) != SOCKET_ERROR ||
        WSAGetLastError() != WSAENOTSOCK ||
        XSocketCreate(AF_INET, SOCK_RAW, IPPROTO_RAW) != INVALID_SOCKET ||
        WSAGetLastError() != WSAESOCKTNOSUPPORT) {
        XNetCleanup();
        return 24;
    }
    XSocketSetLastError(WSAEINTR);
    if (XSocketGetLastError() != WSAEINTR) {
        XNetCleanup();
        return 25;
    }
    const uint32_t tcp = XboxNetSocketCreate(1);
    const uint32_t udp = XboxNetSocketCreate(2);
    if (!tcp || !udp || tcp == udp) {
        if (tcp) XboxNetSocketClose(tcp);
        if (udp) XboxNetSocketClose(udp);
        XboxNetCleanup();
        return 3;
    }
    if (XboxNetSocketSetNonBlocking(udp, 1) != 0) { XboxNetSocketClose(tcp); XboxNetSocketClose(udp); XboxNetCleanup(); return 4; }
    XboxNetSocketClose(tcp);
    XboxNetSocketClose(udp);

    // Exercise the XSocket-facing token ABI with a real UDP loopback datagram.
    const SOCKET receiver = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET sender = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiver == INVALID_SOCKET || sender == INVALID_SOCKET) {
        if (receiver != INVALID_SOCKET) XSocketClose(receiver);
        if (sender != INVALID_SOCKET) XSocketClose(sender);
        XNetCleanup();
        return 5;
    }
    DWORD timeout = 2000;
    WSAEVENT udpEvent = XWSACreateEvent();
    if (!udpEvent || XWSAEventSelect(receiver, udpEvent, FD_READ) != 0) {
        if (udpEvent) XWSACloseEvent(udpEvent);
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 30;
    }
    if (XSocketSetSockOpt(receiver, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 6;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (XSocketBind(receiver, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 7;
    }
    int localLength = sizeof(local);
    if (XSocketGetSockName(receiver, reinterpret_cast<sockaddr*>(&local), &localLength) != 0) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 8;
    }
    const char payload[] = "insignia-xnet-smoke";
    if (XSocketSendTo(sender, payload, sizeof(payload), 0,
            reinterpret_cast<sockaddr*>(&local), sizeof(local)) != sizeof(payload)) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 9;
    }
    if (XWSAWaitForMultipleEvents(1, &udpEvent, TRUE, 2000, FALSE) != WSA_WAIT_EVENT_0) {
        XWSACloseEvent(udpEvent); XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 31;
    }
    char received[64]{};
    sockaddr_in peer{};
    int peerLength = sizeof(peer);
    const int bytes = XSocketRecvFrom(receiver, received, sizeof(received), 0,
        reinterpret_cast<sockaddr*>(&peer), &peerLength);
    if (bytes != sizeof(payload) || std::memcmp(received, payload, sizeof(payload)) != 0) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 10;
    }
    XWSACloseEvent(udpEvent);
    XSocketClose(receiver);
    XSocketClose(sender);

    // Exercise the TCP listen/connect/accept/send/receive path on loopback.
    const SOCKET listener = XSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const SOCKET client = XSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET || client == INVALID_SOCKET) {
        if (listener != INVALID_SOCKET) XSocketClose(listener);
        if (client != INVALID_SOCKET) XSocketClose(client);
        XNetCleanup();
        return 15;
    }
    sockaddr_in listenAddress{};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (XSocketBind(listener, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) != 0 ||
        XSocketListen(listener, 4) != 0) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 16;
    }
    int listenLength = sizeof(listenAddress);
    if (XSocketGetSockName(listener, reinterpret_cast<sockaddr*>(&listenAddress), &listenLength) != 0 ||
        XSocketConnect(client, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) != 0) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 17;
    }
    fd_set readable{};
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    timeval selectTimeout{};
    selectTimeout.tv_sec = 2;
    const int selected = XSocketSelect(0, &readable, nullptr, nullptr, &selectTimeout);
    if (selected != 1 || !FD_ISSET(listener, &readable) ||
        !XWSAFDIsSet(listener, &readable)) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 26;
    }
    const SOCKET accepted = XSocketAccept(listener, nullptr, nullptr);
    if (accepted == INVALID_SOCKET) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 18;
    }
    const char tcpPayload[] = "xnet-tcp-loopback";
    if (XSocketSend(client, tcpPayload, sizeof(tcpPayload), 0) != sizeof(tcpPayload)) {
        XSocketClose(accepted); XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 19;
    }
    char tcpReceived[64]{};
    if (XSocketRecv(accepted, tcpReceived, sizeof(tcpReceived), 0) != sizeof(tcpPayload) ||
        std::memcmp(tcpReceived, tcpPayload, sizeof(tcpPayload)) != 0) {
        XSocketClose(accepted); XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 20;
    }
    XSocketClose(accepted);
    XSocketClose(listener);
    XSocketClose(client);

    if (XWSACleanup() != 0) {
        XNetCleanup();
        return 32;
    }

    // Final cleanup must not tear Winsock down while a DNS worker is active.
    WSAEVENT cleanupDnsEvent = WSACreateEvent();
    XNDNS* cleanupDns = nullptr;
    if (!cleanupDnsEvent || XNetDnsLookup("localhost", cleanupDnsEvent, &cleanupDns) != 0 ||
        !cleanupDns) {
        if (cleanupDns) XNetDnsRelease(cleanupDns);
        if (cleanupDnsEvent) WSACloseEvent(cleanupDnsEvent);
        XNetCleanup();
        return 21;
    }
    XNetCleanup();
    if (WSAWaitForMultipleEvents(1, &cleanupDnsEvent, TRUE, 5000, FALSE) != WSA_WAIT_EVENT_0 ||
        cleanupDns->iStatus != 0 || cleanupDns->cina == 0) {
        XNetDnsRelease(cleanupDns);
        WSACloseEvent(cleanupDnsEvent);
        return 22;
    }
    XNetDnsRelease(cleanupDns);
    WSACloseEvent(cleanupDnsEvent);
    std::puts("network_smoke: async DNS, TCP/UDP loopback, key lifecycle, and socket error checks passed");
    return 0;
}
