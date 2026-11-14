#include <cstdint>
#include <cstdio>
#include <cstring>
#include "network_compat.h"
#include "xnet_compat.h"

int main() {
    XNetStartupParams params{};
    params.cfgSizeOfStruct = sizeof(params);
    params.cfgSockMaxSockets = 4;
    if (XNetStartup(&params) != 0) return 1;

    WSADATA guestWsaData{};
    if (XboxWSAStartup(MAKEWORD(2, 2), &guestWsaData) != 0 ||
        guestWsaData.wHighVersion != MAKEWORD(2, 2) || XboxWSACleanup() != 0) {
        XNetCleanup();
        return 22;
    }

    // The XDK-compatible public winsockx.h defines 12 one-byte startup fields.
    static_assert(sizeof(XNetStartupParams) == 12, "retail XNet startup ABI");
    XNetStartupParams badParams{};
    badParams.cfgSizeOfStruct = 13;
    if (XNetStartup(&badParams) != WSAEFAULT) return 34;

    XNKID generatedId{};
    XNKEY generatedKey{};
    if (XNetCreateKey(&generatedId, &generatedKey) != 0 ||
        (generatedId.ab[0] & 0xF0u) != 0 ||
        XNetRegisterKey(&generatedId, &generatedKey) != 0 ||
        XNetRegisterKey(&generatedId, &generatedKey) != WSAEALREADY ||
        XNetUnregisterKey(&generatedId) != 0 ||
        XNetUnregisterKey(&generatedId) != WSAEINVAL) {
        XNetCleanup();
        return 35;
    }

    BYTE randomBytes[32]{};
    if (XNetRandom(randomBytes, sizeof(randomBytes)) != 0) { XNetCleanup(); return 18; }
    XNADDR testAddress{};
    XNKID testKey{};
    IN_ADDR testIp{};
    if (XNetXnAddrToInAddr(&testAddress, &testKey, &testIp) != WSAEOPNOTSUPP ||
        XNetInAddrToXnAddr(testIp, &testAddress, &testKey) != WSAEOPNOTSUPP ||
        XNetServerToInAddr(testIp, 0, &testIp) != WSAEOPNOTSUPP ||
        XNetUnregisterInAddr(testIp) != WSAEOPNOTSUPP) {
        XNetCleanup();
        return 19;
    }

    WSAEVENT wrappedEvent = XboxWSACreateEvent();
    if (!wrappedEvent || !XboxWSASetEvent(wrappedEvent) ||
        XboxWSAWaitForMultipleEvents(1, &wrappedEvent, TRUE, 0, FALSE) != WSA_WAIT_EVENT_0 ||
        !XboxWSAResetEvent(wrappedEvent) || !XboxWSACloseEvent(wrappedEvent)) {
        if (wrappedEvent) XboxWSACloseEvent(wrappedEvent);
        XNetCleanup();
        return 20;
    }
    XboxWSASetLastError(WSAEINVAL);
    if (XboxWSAGetLastError() != WSAEINVAL ||
        XboxWSAInetAddr("127.0.0.1") != inet_addr("127.0.0.1")) {
        XNetCleanup();
        return 21;
    }

    WSAEVENT dnsEvent = WSACreateEvent();
    XNDNS* dns = nullptr;
    if (!dnsEvent || XNetDnsLookup("localhost", dnsEvent, &dns) != 0 ||
        !dns || WSAWaitForMultipleEvents(1, &dnsEvent, TRUE, 5000, FALSE) != WSA_WAIT_EVENT_0 ||
        dns->iStatus != 0 || dns->cina == 0) {
        if (dns) XNetDnsRelease(dns);
        if (dnsEvent) WSACloseEvent(dnsEvent);
        XNetCleanup();
        return 1;
    }
    XNetDnsRelease(dns);
    WSACloseEvent(dnsEvent);

    // DNS completion is asynchronous: the returned XNDNS remains owned by
    // the caller and is consumed only after its event is signaled.
    WSAEVENT asyncDnsEvent = WSACreateEvent();
    XNDNS* asyncDns = nullptr;
    if (!asyncDnsEvent || XNetDnsLookup("localhost", asyncDnsEvent, &asyncDns) != 0 ||
        !asyncDns || WSAWaitForMultipleEvents(1, &asyncDnsEvent, TRUE, 5000, FALSE) != WSA_WAIT_EVENT_0 ||
        asyncDns->iStatus != 0 || asyncDns->cina == 0) {
        if (asyncDns) XNetDnsRelease(asyncDns);
        if (asyncDnsEvent) WSACloseEvent(asyncDnsEvent);
        XNetCleanup();
        return 28;
    }
    XNetDnsRelease(asyncDns);
    WSACloseEvent(asyncDnsEvent);

    // Releasing an in-flight DNS result must not free memory under its worker.
    WSAEVENT earlyReleaseEvent = WSACreateEvent();
    XNDNS* earlyReleaseDns = nullptr;
    if (!earlyReleaseEvent || XNetDnsLookup("localhost", earlyReleaseEvent, &earlyReleaseDns) != 0 ||
        !earlyReleaseDns || XNetDnsRelease(earlyReleaseDns) != 0 ||
        WSAWaitForMultipleEvents(1, &earlyReleaseEvent, TRUE, 5000, FALSE) != WSA_WAIT_EVENT_0) {
        if (earlyReleaseDns) XNetDnsRelease(earlyReleaseDns);
        if (earlyReleaseEvent) WSACloseEvent(earlyReleaseEvent);
        XNetCleanup();
        return 39;
    }
    WSACloseEvent(earlyReleaseEvent);

    XNADDR titleAddress{};
    const DWORD titleAddressFlags = XNetGetTitleXnAddr(&titleAddress);
    if (titleAddressFlags != XNET_GET_XNADDR_NONE &&
        !(titleAddressFlags & XNET_GET_XNADDR_ETHERNET)) {
        XNetCleanup();
        return 29;
    }

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


    // Validate select() translates guest tokens instead of exposing native handles.
    const SOCKET selectReceiver = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET selectSender = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (selectReceiver == INVALID_SOCKET || selectSender == INVALID_SOCKET) {
        if (selectReceiver != INVALID_SOCKET) XSocketClose(selectReceiver);
        if (selectSender != INVALID_SOCKET) XSocketClose(selectSender);
        XNetCleanup();
        return 23;
    }
    sockaddr_in selectAddress{};
    selectAddress.sin_family = AF_INET;
    selectAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (XSocketBind(selectReceiver, reinterpret_cast<sockaddr*>(&selectAddress),
            sizeof(selectAddress)) != 0) {
        XSocketClose(selectReceiver); XSocketClose(selectSender); XNetCleanup(); return 24;
    }
    int selectAddressLength = sizeof(selectAddress);
    if (XSocketGetSockName(selectReceiver, reinterpret_cast<sockaddr*>(&selectAddress),
            &selectAddressLength) != 0 ||
        XSocketSendTo(selectSender, "s", 2, 0, reinterpret_cast<sockaddr*>(&selectAddress),
            sizeof(selectAddress)) != 2) {
        XSocketClose(selectReceiver); XSocketClose(selectSender); XNetCleanup(); return 25;
    }
    fd_set readable{};
    FD_ZERO(&readable);
    FD_SET(selectReceiver, &readable);
    timeval selectTimeout{2, 0};
    if (XSocketSelect(0, &readable, nullptr, nullptr, &selectTimeout) != 1 ||
        readable.fd_count != 1 || readable.fd_array[0] != selectReceiver) {
        XSocketClose(selectReceiver); XSocketClose(selectSender); XNetCleanup(); return 26;
    }
    char selectPayload[4]{};
    if (XSocketRecv(selectReceiver, selectPayload, sizeof(selectPayload), 0) != 2 ||
        std::memcmp(selectPayload, "s", 2) != 0) {
        XSocketClose(selectReceiver); XSocketClose(selectSender); XNetCleanup(); return 27;
    }
    XSocketClose(selectReceiver);
    XSocketClose(selectSender);

    // Invalid optional address/length pairs must fail immediately, not
    // silently consume a datagram or write through a missing length pointer.
    const SOCKET argumentSocket = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (argumentSocket == INVALID_SOCKET) { XNetCleanup(); return 30; }
    sockaddr_in invalidPeer{};
    if (XSocketRecvFrom(argumentSocket, selectPayload, sizeof(selectPayload), 0,
            reinterpret_cast<sockaddr*>(&invalidPeer), nullptr) != SOCKET_ERROR ||
        WSAGetLastError() != WSAEFAULT) {
        XSocketClose(argumentSocket); XNetCleanup(); return 31;
    }
    int shortPeerLength = static_cast<int>(sizeof(sockaddr_in) - 1);
    if (XSocketRecvFrom(argumentSocket, selectPayload, sizeof(selectPayload), 0,
            reinterpret_cast<sockaddr*>(&invalidPeer), &shortPeerLength) != SOCKET_ERROR ||
        WSAGetLastError() != WSAEFAULT) {
        XSocketClose(argumentSocket); XNetCleanup(); return 32;
    }
    XSocketClose(argumentSocket);

    IN_ADDR loopbackAddress{};
    loopbackAddress.s_addr = htonl(INADDR_LOOPBACK);
    char addressText[16]{};
    if (XNetInAddrToString(loopbackAddress, addressText, sizeof(addressText)) != 0 ||
        std::strcmp(addressText, "127.0.0.1") != 0) {
        XNetCleanup(); return 33;
    }

    // Enforce the title-configured XNet socket ceiling (including accepted sockets).
    SOCKET limited[4]{};
    for (int i = 0; i < 4; ++i) {
        limited[i] = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (limited[i] == INVALID_SOCKET) {
            for (int j = 0; j < i; ++j) XSocketClose(limited[j]);
            XNetCleanup();
            return 36;
        }
    }
    if (XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_UDP) != INVALID_SOCKET ||
        WSAGetLastError() != WSAEMFILE) {
        for (SOCKET socket : limited) XSocketClose(socket);
        XNetCleanup();
        return 37;
    }
    for (SOCKET socket : limited) {
        if (XSocketClose(socket) != 0) { XNetCleanup(); return 38; }
    }

    XNetCleanup();
    std::puts("network_smoke: XNet startup/DNS event, XSocket UDP/TCP/select loopback, byte-order helpers passed");
    return 0;
}
