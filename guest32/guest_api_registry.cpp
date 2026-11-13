#include "guest_api_registry.h"
#include "xnet_compat.h"

#include <cstring>
#include <cstdint>
#include <iterator>
#include <cstring>
#include <cwchar>

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


namespace {
bool GuestRange(std::uint32_t base, std::uint32_t size,
                std::uint32_t address, std::uint32_t bytes) noexcept {
    return address >= base &&
        std::uint64_t(address) + bytes <= std::uint64_t(base) + size;
}
bool IsXnetLibrary(std::uint32_t base, std::uint32_t size,
                   std::uint32_t address) noexcept {
    if (!GuestRange(base, size, address, 2)) return false;
    const wchar_t* name = reinterpret_cast<const wchar_t*>(
        static_cast<std::uintptr_t>(address));
    wchar_t ascii[32]{};
    std::size_t n = 0;
    for (; n + 1 < std::size(ascii); ++n) {
        if (!GuestRange(base, size, address + static_cast<std::uint32_t>(n * 2), 2))
            return false;
        const wchar_t c = name[n];
        if (!c) break;
        if (c > 0x7Fu) return false;
        ascii[n] = static_cast<wchar_t>(std::towupper(c));
    }
    if (n + 1 == std::size(ascii) || name[n] != L'\\0') return false;
    const std::wstring value(ascii);
    return value == L"XNET" || value == L"XNET.DLL";
}
}

extern "C" bool __cdecl XboxGuestResolveNonKernelImports(
    std::uint32_t imageBase, std::uint32_t imageSize,
    std::uint32_t directoryAddress) {
    if (!imageBase || !imageSize ||
        std::uint64_t(imageBase) + imageSize > 0x100000000ull)
        return false;
    if (!directoryAddress) return true;

    struct ImportDescriptor {
        std::uint32_t firstThunk;
        std::uint32_t wideCharName;
    };
    static_assert(sizeof(ImportDescriptor) == 8, "XBE import descriptor size");

    bool terminatedDescriptors = false;
    for (std::uint32_t descriptorIndex = 0; descriptorIndex < 64;
         ++descriptorIndex) {
        const std::uint64_t descriptorAddress64 =
            std::uint64_t(directoryAddress) + descriptorIndex * sizeof(ImportDescriptor);
        if (descriptorAddress64 > 0xFFFFFFFFull) return false;
        const auto descriptorAddress = static_cast<std::uint32_t>(descriptorAddress64);
        if (!GuestRange(imageBase, imageSize, descriptorAddress,
                        sizeof(ImportDescriptor)))
            return false;
        ImportDescriptor descriptor{};
        std::memcpy(&descriptor,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(descriptorAddress)),
            sizeof(descriptor));
        if (!descriptor.firstThunk) {
            terminatedDescriptors = descriptor.wideCharName == 0;
            break;
        }
        if (!descriptor.wideCharName ||
            !IsXnetLibrary(imageBase, imageSize, descriptor.wideCharName) ||
            !GuestRange(imageBase, imageSize, descriptor.firstThunk, 4))
            return false;

        bool terminatedThunks = false;
        for (std::uint32_t thunkIndex = 0; thunkIndex < 4096; ++thunkIndex) {
            const std::uint64_t thunkAddress64 =
                std::uint64_t(descriptor.firstThunk) + thunkIndex * 4ull;
            if (thunkAddress64 > 0xFFFFFFFFull) return false;
            const auto thunkAddress = static_cast<std::uint32_t>(thunkAddress64);
            if (!GuestRange(imageBase, imageSize, thunkAddress, 4)) return false;

            std::uint32_t importNameAddress = 0;
            std::memcpy(&importNameAddress,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(thunkAddress)),
                sizeof(importNameAddress));
            if (!importNameAddress) {
                terminatedThunks = true;
                break;
            }
            // XNet's named imports are supported. Ordinal imports require an
            // authoritative per-library ordinal map and are intentionally rejected.
            if (importNameAddress & 0x80000000u) return false;
            if (!GuestRange(imageBase, imageSize, importNameAddress, 3)) return false;

            const char* functionName = reinterpret_cast<const char*>(
                static_cast<std::uintptr_t>(importNameAddress + 2u));
            const std::uint32_t maxName =
                imageBase + imageSize - (importNameAddress + 2u);
            const void* terminator = std::memchr(functionName, 0, maxName);
            if (!terminator) return false;
            const std::uint32_t address = XboxGuestApiLookup(functionName);
            if (!address) return false;
            std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(thunkAddress)),
                        &address, sizeof(address));
        }
        if (!terminatedThunks) return false;
    }
    return terminatedDescriptors;
}
