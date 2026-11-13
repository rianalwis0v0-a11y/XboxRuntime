#include "guest_api_registry.h"
#include "xnet_compat.h"

#include <cassert>
#include <cstdint>

int main() {
    static_assert(sizeof(void*) == 4, "registry test must run as x86");
    assert(XboxGuestApiCount() == 29u);
    assert(XboxGuestApiLookup(nullptr) == 0u);
    assert(XboxGuestApiLookup("") == 0u);
    assert(XboxGuestApiLookup("XNetDefinitelyNotAnApi") == 0u);

    const char* names[] = {
        "XNetStartup", "XNetCleanup", "XNetDnsLookup", "XNetDnsRelease",
        "XNetInAddrToString", "XNetServerToInAddr", "XNetInAddrToServer",
        "XNetGetEthernetLinkStatus", "XNetGetTitleXnAddr",
        "XSocketCreate", "XSocketClose", "XSocketBind", "XSocketConnect",
        "XSocketListen", "XSocketAccept", "XSocketShutdown",
        "XSocketIOCTLSocket", "XSocketSetSockOpt", "XSocketGetSockOpt",
        "XSocketGetSockName", "XSocketGetPeerName", "XSocketSend",
        "XSocketRecv", "XSocketSendTo", "XSocketRecvFrom",
        "XSocketHTONL", "XSocketNTOHL", "XSocketHTONS", "XSocketNTOHS"
    };
    for (const char* name : names) assert(XboxGuestApiLookup(name) != 0u);

    using Convert32 = u_long (WINAPI*)(u_long);
    using Convert16 = u_short (WINAPI*)(u_short);
    auto htonl = reinterpret_cast<Convert32>(
        static_cast<std::uintptr_t>(XboxGuestApiLookup("XSocketHTONL")));
    auto ntohl = reinterpret_cast<Convert32>(
        static_cast<std::uintptr_t>(XboxGuestApiLookup("XSocketNTOHL")));
    auto htons = reinterpret_cast<Convert16>(
        static_cast<std::uintptr_t>(XboxGuestApiLookup("XSocketHTONS")));
    auto ntohs = reinterpret_cast<Convert16>(
        static_cast<std::uintptr_t>(XboxGuestApiLookup("XSocketNTOHS")));
    assert(htonl && ntohl && htons && ntohs);
    assert(ntohl(htonl(0x12345678u)) == 0x12345678u);
    assert(ntohs(htons(0x1234u)) == 0x1234u);
    return 0;
}
