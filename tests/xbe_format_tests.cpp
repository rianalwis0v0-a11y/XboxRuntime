#include "../modules/xbe_loader/xbe_format.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
void Write32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

bool CheckType(std::uint32_t entryKey, std::uint32_t thunkKey,
               bool expectDebug, bool expectChihiro) {
    constexpr std::uint32_t base = 0x10000000u;
    constexpr std::uint32_t entry = base + 0x1000u;
    std::vector<std::uint8_t> file(0x410u, 0);
    Write32(file, 0x000, XboxXbe::kMagic);
    Write32(file, 0x104, base);
    Write32(file, 0x108, 0x400u);
    Write32(file, 0x10C, 0x2000u);
    Write32(file, 0x110, 0x178u);
    Write32(file, 0x118, base + 0x200u);
    Write32(file, 0x11C, 1u);
    Write32(file, 0x120, base + 0x300u);
    Write32(file, 0x128, entry ^ entryKey);
    Write32(file, 0x158, thunkKey);

    Write32(file, 0x200, 0xD0u); // Certificate size.
    Write32(file, 0x300, 0x04u); // Executable section.
    Write32(file, 0x304, base + 0x1000u);
    Write32(file, 0x308, 0x1000u);
    Write32(file, 0x30C, 0x400u);
    Write32(file, 0x310, 0x10u);

    XboxXbe::Image image{};
    const auto result = XboxXbe::Parse(file.data(), file.size(), &image);
    if (!result) {
        std::fprintf(stderr, "FAIL: Parse error=%s detail=0x%08X\n",
                     XboxXbe::ErrorName(result.error), result.detail);
        return false;
    }
    if (image.entryPoint != entry || image.debugImage != expectDebug ||
        image.chihiroImage != expectChihiro || image.kernelThunkAddress != 0) {
        std::fprintf(stderr,
            "FAIL: decoded entry=0x%08X debug=%u chihiro=%u thunk=0x%08X\n",
            image.entryPoint, image.debugImage ? 1u : 0u,
            image.chihiroImage ? 1u : 0u, image.kernelThunkAddress);
        return false;
    }
    return true;
}
}

int main() {
    if (!CheckType(XboxXbe::kRetailEntryXor,
                   XboxXbe::kRetailThunkXor, false, false))
        return 1;
    if (!CheckType(XboxXbe::kDebugEntryXor,
                   XboxXbe::kDebugThunkXor, true, false))
        return 2;
    if (!CheckType(XboxXbe::kChihiroEntryXor,
                   XboxXbe::kChihiroThunkXor, false, true))
        return 3;
    std::puts("PASS: retail, debug, and Chihiro XBE type/key decoding matches Cxbx semantics.");
    return 0;
}
