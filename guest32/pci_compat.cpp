#include "pci_compat.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace {
constexpr std::size_t kPciDeviceCount = 32;
constexpr std::size_t kPciFunctionCount = 8;
constexpr std::size_t kPciConfigBytes = 256;
constexpr ULONG kXboxHostBridgeSlot = 0x60u; // PCI device 0, function 3.
constexpr ULONG kXboxHostBridgeFsbRegister = 0x6Cu;
constexpr std::uint32_t kXboxHostBridgeFsbRatio = 0x00000801u;

using PciFunction = std::array<std::uint8_t, kPciConfigBytes>;
using PciBus = std::array<std::array<PciFunction, kPciFunctionCount>,
                          kPciDeviceCount>;

PciBus MakeInitialPciBus() {
    PciBus bus{};
    for (auto& device : bus) {
        for (auto& function : device)
            function.fill(0xFFu);
    }

    // NXDK's CRT reads the Xbox host bridge's 0:0.3 register 0x6C to
    // calculate the 133.333 MHz FSB: 16.666... MHz / 1 * 8.
    auto& hostBridgeFunction3 = bus[0][3];
    hostBridgeFunction3[kXboxHostBridgeFsbRegister + 0] = 0x01u;
    hostBridgeFunction3[kXboxHostBridgeFsbRegister + 1] = 0x08u;
    hostBridgeFunction3[kXboxHostBridgeFsbRegister + 2] = 0x00u;
    hostBridgeFunction3[kXboxHostBridgeFsbRegister + 3] = 0x00u;
    return bus;
}

PciBus g_pciBus0 = MakeInitialPciBus();
std::mutex g_pciMutex;
}

extern "C" void __stdcall K_HalReadWritePCISpace(
    ULONG busNumber,
    ULONG slotNumber,
    ULONG registerNumber,
    PVOID buffer,
    ULONG length,
    BOOLEAN writePciSpace) {
    if (!buffer || length == 0)
        return;

    auto* bytes = static_cast<std::uint8_t*>(buffer);
    const ULONG device = slotNumber & 0x1Fu;
    const ULONG function = (slotNumber >> 5u) & 0x07u;
    const bool supportedFunction =
        busNumber == 0 && device == 0 && function == 3;

    std::lock_guard<std::mutex> lock(g_pciMutex);
    for (ULONG i = 0; i < length; ++i) {
        const std::uint64_t offset =
            static_cast<std::uint64_t>(registerNumber) + i;
        if (!supportedFunction || offset >= kPciConfigBytes) {
            if (!writePciSpace)
                bytes[i] = 0xFFu;
            continue;
        }

        auto& value = g_pciBus0[device][function][static_cast<std::size_t>(offset)];
        if (writePciSpace)
            value = bytes[i];
        else
            bytes[i] = value;
    }
}

static_assert(kXboxHostBridgeFsbRatio == 0x00000801u,
              "NXDK Xbox FSB divider/multiplier encoding changed");
