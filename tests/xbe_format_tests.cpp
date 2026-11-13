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
        Put32(file, s + 0x04, 0x00011000 + i * 0x800);
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

    auto debugFile = file;
    Put32(debugFile, 0x128, 0x00011020 ^ kDebugEntryXor);
    Put32(debugFile, 0x158, kDebugThunkXor);
    Image debugImage{};
    result = Parse(debugFile.data(), debugFile.size(), &debugImage);
    assert(result);
    assert(debugImage.debugImage);
    assert(debugImage.entryPoint == 0x11020);
    assert(debugImage.kernelThunkAddress == 0);
    assert(!debugImage.chihiroImage);

    // Cxbx identifies Chihiro from the encoded entry-point high nibble and
    // uses the Chihiro-specific XOR pair for both entry point and thunk.
    auto chihiroFile = file;
    Put32(chihiroFile, 0x128, 0x00011030 ^ kChihiroEntryXor);
    Put32(chihiroFile, 0x158, kChihiroThunkXor); // decoded null thunk table
    Image chihiroImage{};
    result = Parse(chihiroFile.data(), chihiroFile.size(), &chihiroImage);
    assert(result);
    assert(chihiroImage.chihiroImage);
    assert(!chihiroImage.debugImage);
    assert(chihiroImage.entryPoint == 0x11030);
    assert(chihiroImage.kernelThunkAddress == 0);

    // The encoded thunk selects the image type. A debug-encoded entry point
    // in a retail-marked image must not cause the parser to guess debug mode.
    auto mismatchedType = file;
    Put32(mismatchedType, 0x128, 0x00011020 ^ kDebugEntryXor);
    assert(Parse(mismatchedType.data(), mismatchedType.size(), &image).error ==
           Error::InvalidEntryPoint);

    // A valid TLS directory lives in the mapped executable section.
    // Its template is in the raw section and its index slot is in zero-fill.
    auto validTls = file;
    Put32(validTls, 0x12C, 0x00011000);
    Put32(validTls, 0x400, 0x00011020); // DataStart
    Put32(validTls, 0x404, 0x00011040); // DataEnd
    Put32(validTls, 0x408, 0x00011040); // TLS index slot (zero-fill)
    Put32(validTls, 0x40C, 0);          // no callback array
    Put32(validTls, 0x410, 0x10);       // zero-fill bytes
    Put32(validTls, 0x414, 0);          // characteristics
    Image tlsImage{};
    assert(Parse(validTls.data(), validTls.size(), &tlsImage));
    assert(tlsImage.tlsAddress == 0x11000);

    // A TLS directory in an unmapped hole inside SizeOfImage must be rejected.
    auto tlsDirectoryInGap = file;
    Put32(tlsDirectoryInGap, 0x12C, 0x00012000);
    assert(Parse(tlsDirectoryInGap.data(), tlsDirectoryInGap.size(), &image).error ==
           Error::InvalidSection);

    // Reject a TLS template whose end precedes its start.
    auto reversedTlsTemplate = validTls;
    Put32(reversedTlsTemplate, 0x404, 0x00011010);
    assert(Parse(reversedTlsTemplate.data(), reversedTlsTemplate.size(), &image).error ==
           Error::InvalidSection);

    auto invalidTls = file;
    Put32(invalidTls, 0x12C, 0x00012FF0);
    assert(Parse(invalidTls.data(), invalidTls.size(), &image).error ==
           Error::InvalidSection);

    Image forged = image;
    forged.headerSize = forged.imageSize + 1;
    std::vector<std::uint8_t> forgedTarget(forged.imageSize, 0);
    assert(MapImage(file.data(), file.size(), forged,
                    forgedTarget.data(), forgedTarget.size()).error ==
           Error::InvalidImageSize);

    auto thunkFile = file;
    Put32(thunkFile, 0x158, 0x00011000 ^ kRetailThunkXor);
    Put32(thunkFile, 0x400, 1);
    Put32(thunkFile, 0x404, 0);
    Image thunkImage{};
    result = Parse(thunkFile.data(), thunkFile.size(), &thunkImage);
    assert(result);
    assert(thunkImage.kernelThunkAddress == 0x11000);

    auto unterminatedThunks = file;
    Put32(unterminatedThunks, 0x158, 0x00011000 ^ kRetailThunkXor);
    Put32(unterminatedThunks, 0x350, 0x200);
    std::memset(unterminatedThunks.data() + 0x400, 0x01, 0x200);
    assert(Parse(unterminatedThunks.data(), unterminatedThunks.size(), &image).error ==
           Error::InvalidThunkTable);

    auto legacyCertificate = file;
    Put32(legacyCertificate, 0x178, 0x0D0); // XBE certificate revision 1
    assert(Parse(legacyCertificate.data(), legacyCertificate.size(), &image));

    auto v4Certificate = file;
    Put32(v4Certificate, 0x108, 0x600);
    Put32(v4Certificate, 0x118, 0x10178);
    Put32(v4Certificate, 0x120, 0x10364);
    Put32(v4Certificate, 0x178, 0x1EC);
    std::memcpy(v4Certificate.data() + 0x364,
                file.data() + 0x348, kSectionHeaderSize);
    Put32(v4Certificate, 0x364 + 0x0C, 0x600);
    std::memset(v4Certificate.data() + 0x600, 0xA5, 0x200);
    Image v4Image{};
    assert(Parse(v4Certificate.data(), v4Certificate.size(), &v4Image));

    auto shortCertificate = file;
    Put32(shortCertificate, 0x178, 4);
    assert(Parse(shortCertificate.data(), shortCertificate.size(), &image).error ==
           Error::InvalidCertificate);

    auto badLibraryVersions = file;
    Put32(badLibraryVersions, 0x160, 1);
    Put32(badLibraryVersions, 0x164, 0xFFFFFFF0);
    assert(Parse(badLibraryVersions.data(), badLibraryVersions.size(), &image).error ==
           Error::InvalidLibraryVersions);

    auto badImportDirectory = file;
    Put32(badImportDirectory, 0x15C, 0xFFFFFFFC);
    assert(Parse(badImportDirectory.data(), badImportDirectory.size(), &image).error ==
           Error::InvalidImportDirectory);

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

    // MapImage must defend itself against forged metadata and avoid
    // partially modifying the destination when validation fails.
    Image forgedOverlap = image;
    forgedOverlap.sectionCount = 2;
    forgedOverlap.sections[1] = forgedOverlap.sections[0];
    std::vector<std::uint8_t> untouched(image.imageSize, 0x5A);
    assert(MapImage(file.data(), file.size(), forgedOverlap,
                    untouched.data(), untouched.size()).error ==
           Error::OverlappingSections);
    assert(untouched[0] == 0x5A && untouched[0x1000] == 0x5A);

    Image forgedHeaderOverlap = image;
    forgedHeaderOverlap.sections[0].virtualAddress =
        forgedHeaderOverlap.baseAddress + 0x100;
    assert(MapImage(file.data(), file.size(), forgedHeaderOverlap,
                    untouched.data(), untouched.size()).error ==
           Error::InvalidSection);
    assert(untouched[0] == 0x5A && untouched[0x100] == 0x5A);

    Image forgedRawRange = image;
    forgedRawRange.sections[0].rawAddress =
        static_cast<std::uint32_t>(file.size() - 1);
    forgedRawRange.sections[0].rawSize = 0x200;
    assert(MapImage(file.data(), file.size(), forgedRawRange,
                    untouched.data(), untouched.size()).error ==
           Error::InvalidSection);
    assert(untouched[0] == 0x5A && untouched[0x1000] == 0x5A);

    std::cout << "XBE format tests passed\n";
    return 0;
}
