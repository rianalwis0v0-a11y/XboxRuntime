#include <cstdint>
#include <cstdio>
#include "network_compat.h"
#include "xnet_compat.h"

int main() {
    XboxXNetStartupParams params{};
    params.cfgSizeOfStruct = sizeof(params);
    if (XboxXNetStartup(&params) != 0) return 1;

    WSAEVENT dnsEvent = WSACreateEvent();
    XboxXnDns* dns = nullptr;
    if (!dnsEvent || XboxXNetDnsLookup("localhost", dnsEvent, &dns) != 0 ||
        !dns || dns->iStatus != 0 || dns->cina == 0 ||
        WSAWaitForMultipleEvents(1, &dnsEvent, TRUE, 0, FALSE) != WSA_WAIT_EVENT_0) {
        if (dns) XboxXNetDnsRelease(dns);
        if (dnsEvent) WSACloseEvent(dnsEvent);
        XboxXNetCleanup();
        return 1;
    }
    XboxXNetDnsRelease(dns);
    WSACloseEvent(dnsEvent);

    uint32_t loopback = 0;
    if (XboxNetResolveIPv4("localhost", &loopback) != 0 || loopback == 0) {
        XboxXNetCleanup();
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
    XboxXNetCleanup();
    std::puts("network_smoke: XNet startup/DNS event, IPv4 DNS, TCP/UDP socket lifecycle passed");
    return 0;
}
