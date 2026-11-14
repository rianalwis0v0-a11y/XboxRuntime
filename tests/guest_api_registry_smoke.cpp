#include "guest_api_registry.h"
#include "xnet_compat.h"

#include <cassert>
#include <cstdint>
#include <cstring>

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

    // Exercise the actual XBE non-kernel import descriptor path. All guest
    // pointers in this fixture are 32-bit VAs into one x86 image buffer.
    alignas(8) static std::uint8_t image[2048]{};
    const auto base = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(image));
    const auto va = [base](std::uint32_t offset) { return base + offset; };
    struct Descriptor { std::uint32_t firstThunk, wideCharName; };
    static_assert(sizeof(Descriptor) == 8, "XBE import descriptor layout");
    const Descriptor descriptor{ va(0x200), va(0x300) };
    std::memcpy(image + 0x100, &descriptor, sizeof(descriptor));
    const Descriptor terminator{};
    std::memcpy(image + 0x108, &terminator, sizeof(terminator));
    const wchar_t libraryName[] = L"XNET.DLL";
    std::memcpy(image + 0x300, libraryName, sizeof(libraryName));
    const std::uint32_t importName = va(0x400);
    std::memcpy(image + 0x200, &importName, sizeof(importName));
    const std::uint32_t endThunk = 0;
    std::memcpy(image + 0x204, &endThunk, sizeof(endThunk));
    const std::uint16_t hint = 0;
    std::memcpy(image + 0x400, &hint, sizeof(hint));
    std::memcpy(image + 0x402, "XNetStartup", sizeof("XNetStartup"));
    assert(XboxGuestResolveNonKernelImports(base, sizeof(image), va(0x100)));
    std::uint32_t resolved = 0;
    std::memcpy(&resolved, image + 0x200, sizeof(resolved));
    assert(resolved == XboxGuestApiLookup("XNetStartup"));

    // Also accept an image-relative thunk name and normalize it to the same VA.
    const std::uint32_t importNameRva = 0x400u;
    std::memcpy(image + 0x200, &importNameRva, sizeof(importNameRva));
    assert(XboxGuestResolveNonKernelImports(base, sizeof(image), va(0x100)));
    std::memcpy(&resolved, image + 0x200, sizeof(resolved));
    assert(resolved == XboxGuestApiLookup("XNetStartup"));

    // Unknown names and unknown non-kernel libraries must fail closed.
    std::memcpy(image + 0x200, &importName, sizeof(importName));
    std::memcpy(image + 0x402, "XNetNotAnApi", sizeof("XNetNotAnApi"));
    assert(!XboxGuestResolveNonKernelImports(base, sizeof(image), va(0x100)));

    return 0;
}
