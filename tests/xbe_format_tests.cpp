#include "../modules/xbe_loader/xbe_format.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using namespace XboxXbe;

static void Put32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                  std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

static std::vector<std::uint8_t> MakeValidXbe(std::uint32_t sectionCount = 1) {
    std::vector<std::uint8_t> file(0x800, 0);
    Put32(file, 0x000, kMagic);
    Put32(file, 0x104, 0x00010000);
    Put32(file, 0x108, 0x400);
    Put32(file, 0x10C, 0x3000);
    Put32(file, 0x110, 0x178);
    Put32(file, 0x118, 0x00010178);
    Put32(file, 0x11C, sectionCount);
    Put32(file, 0x120, 0x00010348);
    Put32(file, 0x128, 0x00011010 ^ kRetailEntryXor);
    Put32(file, 0x12C, 0);
    Put32(file, 0x158, kRetailThunkXor); // decoded null thunk table
    Put32(file, 0x178, 0x1D0);

    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const std::size_t s = 0x348 + i * kSectionHeaderSize;
        Put32(file, s + 0x00, 0x04); // executable
        Put32(file, s + 0x04, 0x00011000 + i * 0x1000);
        Put32(file, s + 0x08, 0x1000);
        Put32(file, s + 0x0C, 0x400 + i * 0x200);
        Put32(file, s + 0x10, 0x200);
        std::memset(file.data() + 0x400 + i * 0x200,
                    static_cast<int>(0xA0 + i), 0x200);
    }
    return file;
}

int main() {
    Image image{};

    auto file = MakeValidXbe();
    Result result = Parse(file.data(), file.size(), &image);
    assert(result);
    assert(image.baseAddress == 0x10000);
    assert(image.imageSize == 0x3000);
    assert(image.entryPoint == 0x11010);
    assert(image.kernelThunkAddress == 0);
    assert(image.sectionCount == 1);
    assert(!image.debugImage);

    std::vector<std::uint8_t> mapped(image.imageSize, 0xCC);
    result = MapImage(file.data(), file.size(), image,
                      mapped.data(), mapped.size());
    assert(result);
    assert(mapped[0] == 'X' && mapped[1] == 'B');
    assert(mapped[0x1000] == 0xA0);
    assert(mapped[0x11FF] == 0xA0);
    assert(mapped[0x1200] == 0x00); // zero-fill remainder of section
    assert(mapped[0x2FFF] == 0x00);

    auto badMagic = file;
    badMagic[0] = 0;
    assert(Parse(badMagic.data(), badMagic.size(), &image).error == Error::BadMagic);

    assert(Parse(file.data(), 32, &image).error == Error::FileTooSmall);

    auto badRawRange = file;
    Put32(badRawRange, 0x348 + 0x0C, 0x7FFFFFF0);
    assert(Parse(badRawRange.data(), badRawRange.size(), &image).error ==
           Error::InvalidSection);

    auto overlap = MakeValidXbe(2);
    assert(Parse(overlap.data(), overlap.size(), &image).error ==
           Error::OverlappingSections);

    std::vector<std::uint8_t> tooSmall(16, 0);
    assert(MapImage(file.data(), file.size(), image,
                    tooSmall.data(), tooSmall.size()).error ==
           Error::OutputTooSmall);

    std::cout << "XBE format tests passed\n";
    return 0;
}
