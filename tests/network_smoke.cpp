#include <cstdint>
#include <cstdio>
#include <cstring>
#include "network_compat.h"
#include "xnet_compat.h"

int main() {
    XNetStartupParams params{};
    params.cfgSizeOfStruct = sizeof(params);
    if (XNetStartup(&params) != 0) return 1;

    WSAEVENT dnsEvent = WSACreateEvent();
    XNDNS* dns = nullptr;
    if (!dnsEvent || XNetDnsLookup("localhost", dnsEvent, &dns) != 0 ||
        !dns || dns->iStatus != 0 || dns->cina == 0 ||
        WSAWaitForMultipleEvents(1, &dnsEvent, TRUE, 0, FALSE) != WSA_WAIT_EVENT_0) {
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

    // Exercise TCP bind/listen/connect/accept/send/recv on an ephemeral port.
    const SOCKET listener = XSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const SOCKET client = XSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET || client == INVALID_SOCKET) {
        if (listener != INVALID_SOCKET) XSocketClose(listener);
        if (client != INVALID_SOCKET) XSocketClose(client);
        XNetCleanup();
        return 11;
    }
    sockaddr_in listenAddress{};
    listenAddress.sin_family = AF_INET;
    listenAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (XSocketBind(listener, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) != 0 ||
        XSocketListen(listener, 1) != 0) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 12;
    }
    int listenLength = sizeof(listenAddress);
    if (XSocketGetSockName(listener, reinterpret_cast<sockaddr*>(&listenAddress), &listenLength) != 0 ||
        XSocketConnect(client, reinterpret_cast<sockaddr*>(&listenAddress), sizeof(listenAddress)) != 0) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 13;
    }
    const SOCKET accepted = XSocketAccept(listener, nullptr, nullptr);
    if (accepted == INVALID_SOCKET) {
        XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 14;
    }
    const char tcpPayload[] = "xnet-tcp-loopback";
    if (XSocketSend(client, tcpPayload, sizeof(tcpPayload), 0) != sizeof(tcpPayload)) {
        XSocketClose(accepted); XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 15;
    }
    char tcpReceived[64]{};
    const int tcpBytes = XSocketRecv(accepted, tcpReceived, sizeof(tcpReceived), 0);
    if (tcpBytes != sizeof(tcpPayload) ||
        std::memcmp(tcpReceived, tcpPayload, sizeof(tcpPayload)) != 0) {
        XSocketClose(accepted); XSocketClose(listener); XSocketClose(client); XNetCleanup(); return 16;
    }
    XSocketClose(accepted);
    XSocketClose(listener);
    XSocketClose(client);

    if (XSocketHTONL(0x01020304ul) != htonl(0x01020304ul) ||
        XSocketNTOHL(htonl(0x01020304ul)) != 0x01020304ul ||
        XSocketHTONS(0x1234u) != htons(0x1234u) ||
        XSocketNTOHS(htons(0x1234u)) != 0x1234u) {
        XNetCleanup();
        return 17;
    }

    XNetCleanup();
    std::puts("network_smoke: XNet startup/DNS event, XSocket UDP/TCP loopback, byte-order helpers passed");
    return 0;
}
