#include <cstdint>
#include <cstdio>
#include "network_compat.h"

int main() {
    if (XboxNetStartup() != 0) return 1;
    uint32_t loopback = 0;
    if (XboxNetResolveIPv4("localhost", &loopback) != 0 || loopback == 0) {
        XboxNetCleanup();
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
    XboxNetSocketClose(tcp);
    XboxNetSocketClose(udp);
    XboxNetCleanup();
    std::puts("network_smoke: startup, IPv4 DNS, TCP/UDP socket lifecycle passed");
    return 0;
}
