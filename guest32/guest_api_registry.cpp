#include "guest_api_registry.h"
#include "xnet_compat.h"

#include <cstring>
#include <cstdint>
#include <iterator>

namespace {
template <typename Function>
std::uint32_t GuestAddress(Function function) noexcept {
    static_assert(sizeof(void*) == 4, "guest API addresses require x86");
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(function));
}
struct Entry {
    const char* name;
    std::uint32_t address;
};
#define GUEST_API(symbol) { #symbol, GuestAddress(&symbol) }
const Entry kGuestApis[] = {
    GUEST_API(XNetStartup),
    GUEST_API(XNetCleanup),
    GUEST_API(XNetDnsLookup),
    GUEST_API(XNetDnsRelease),
    GUEST_API(XNetInAddrToString),
    GUEST_API(XNetServerToInAddr),
    GUEST_API(XNetInAddrToServer),
    GUEST_API(XNetGetEthernetLinkStatus),
    GUEST_API(XNetGetTitleXnAddr),
    GUEST_API(XSocketCreate),
    GUEST_API(XSocketClose),
    GUEST_API(XSocketBind),
    GUEST_API(XSocketConnect),
    GUEST_API(XSocketListen),
    GUEST_API(XSocketAccept),
    GUEST_API(XSocketShutdown),
    GUEST_API(XSocketIOCTLSocket),
    GUEST_API(XSocketSetSockOpt),
    GUEST_API(XSocketGetSockOpt),
    GUEST_API(XSocketGetSockName),
    GUEST_API(XSocketGetPeerName),
    GUEST_API(XSocketSend),
    GUEST_API(XSocketRecv),
    GUEST_API(XSocketSendTo),
    GUEST_API(XSocketRecvFrom),
    GUEST_API(XSocketHTONL),
    GUEST_API(XSocketNTOHL),
    GUEST_API(XSocketHTONS),
    GUEST_API(XSocketNTOHS),
};
#undef GUEST_API
}

extern "C" std::uint32_t __cdecl XboxGuestApiLookup(const char* symbolName) {
    if (!symbolName || !*symbolName) return 0;
    for (const Entry& entry : kGuestApis) {
        if (std::strcmp(symbolName, entry.name) == 0) return entry.address;
    }
    return 0;
}

extern "C" std::uint32_t __cdecl XboxGuestApiCount() {
    return static_cast<std::uint32_t>(std::size(kGuestApis));
}
