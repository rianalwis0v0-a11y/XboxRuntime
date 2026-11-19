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
    char received[64]{};
    sockaddr_in peer{};
    int peerLength = sizeof(peer);
    const int bytes = XSocketRecvFrom(receiver, received, sizeof(received), 0,
        reinterpret_cast<sockaddr*>(&peer), &peerLength);
    if (bytes != sizeof(payload) || std::memcmp(received, payload, sizeof(payload)) != 0) {
        XSocketClose(receiver); XSocketClose(sender); XNetCleanup(); return 10;
    }
    XSocketClose(receiver);
    XSocketClose(sender);
    XNetCleanup();
    std::puts("network_smoke: XNet startup/DNS event, XSocket UDP loopback, TCP/UDP token lifecycle passed");
    return 0;
}
