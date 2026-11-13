#include "xbe_format.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace XboxXbe {
namespace {
std::uint32_t Read32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool AddOverflows(std::uint32_t a, std::uint32_t b) noexcept {
    return b > (std::numeric_limits<std::uint32_t>::max)() - a;
}

bool RangeInside(std::size_t offset, std::size_t length,
                 std::size_t total) noexcept {
    return offset <= total && length <= total - offset;
}

bool VaRangeToFile(std::uint32_t address, std::uint32_t base,
                   std::uint32_t headerSize, std::size_t length,
                   std::size_t fileSize, std::size_t* offset) noexcept {
    if (address < base) return false;
    const std::uint32_t delta = address - base;
    if (delta > headerSize || length > static_cast<std::size_t>(headerSize - delta))
        return false;
    if (!RangeInside(delta, length, fileSize)) return false;
    if (offset) *offset = delta;
    return true;
}
} // namespace

bool HeaderVaToFileOffset(std::uint32_t address, std::uint32_t baseAddress,
                          std::uint32_t headerSize, std::size_t length,
                          std::size_t* fileOffset) noexcept {
    if (address < baseAddress) return false;
    const std::uint32_t delta = address - baseAddress;
    if (delta > headerSize || length > static_cast<std::size_t>(headerSize - delta))
        return false;
    if (fileOffset) *fileOffset = delta;
    return true;
}

Result Parse(const std::uint8_t* file, std::size_t fileSize,
             Image* output) noexcept {
    if (!file || !output) return {Error::NullArgument, 0};
    if (fileSize < kMinimumHeaderSize) return {Error::FileTooSmall, 0};
    if (Read32(file) != kMagic) return {Error::BadMagic, 0};

    Image parsed{};
    parsed.baseAddress = Read32(file + 0x104);
    parsed.headerSize = Read32(file + 0x108);
    parsed.imageSize = Read32(file + 0x10C);
    parsed.imageHeaderSize = Read32(file + 0x110);
    parsed.certificateAddress = Read32(file + 0x118);
    parsed.sectionCount = Read32(file + 0x11C);
    const std::uint32_t sectionTableAddress = Read32(file + 0x120);
    const std::uint32_t encodedEntry = Read32(file + 0x128);
    const std::uint32_t encodedThunk = Read32(file + 0x158);

    if (parsed.imageHeaderSize < kMinimumHeaderSize ||
        parsed.imageHeaderSize > parsed.headerSize ||
        parsed.headerSize > fileSize)
        return {Error::InvalidHeaderSize, parsed.headerSize};

    if (parsed.imageSize < parsed.headerSize ||
        parsed.baseAddress == 0 ||
        AddOverflows(parsed.baseAddress, parsed.imageSize))
        return {Error::InvalidImageSize, parsed.imageSize};

    if (parsed.sectionCount == 0 || parsed.sectionCount > kMaximumSections)
        return {Error::InvalidSectionCount, parsed.sectionCount};

    const std::uint64_t tableBytes =
        static_cast<std::uint64_t>(parsed.sectionCount) * kSectionHeaderSize;
    std::size_t sectionTableOffset = 0;
    if (tableBytes > (std::numeric_limits<std::size_t>::max)() ||
        !VaRangeToFile(sectionTableAddress, parsed.baseAddress,
                       parsed.headerSize, static_cast<std::size_t>(tableBytes),
                       fileSize, &sectionTableOffset))
        return {Error::SectionTableOutOfRange, sectionTableAddress};

    // The certificate begins with its own byte size. Validate the complete
    // structure against the header-backed file region before trusting it.
    std::size_t certificateOffset = 0;
    if (!VaRangeToFile(parsed.certificateAddress, parsed.baseAddress,
                       parsed.headerSize, sizeof(std::uint32_t),
                       fileSize, &certificateOffset))
        return {Error::InvalidCertificate, parsed.certificateAddress};
    const std::uint32_t certificateSize = Read32(file + certificateOffset);
    if (certificateSize < 4 ||
        !VaRangeToFile(parsed.certificateAddress, parsed.baseAddress,
                       parsed.headerSize, certificateSize, fileSize, nullptr))
        return {Error::InvalidCertificate, certificateSize};

    const std::uint32_t retailEntry = encodedEntry ^ kRetailEntryXor;
    const std::uint32_t debugEntry = encodedEntry ^ kDebugEntryXor;
    const std::uint32_t retailThunk = encodedThunk ^ kRetailThunkXor;
    const std::uint32_t debugThunk = encodedThunk ^ kDebugThunkXor;

    // Section metadata is decoded before choosing the retail/debug key.
    for (std::uint32_t i = 0; i < parsed.sectionCount; ++i) {
        const std::uint8_t* p = file + sectionTableOffset +
                                static_cast<std::size_t>(i) * kSectionHeaderSize;
        Section& s = parsed.sections[i];
        s.flags = Read32(p + 0x00);
        s.virtualAddress = Read32(p + 0x04);
        s.virtualSize = Read32(p + 0x08);
        s.rawAddress = Read32(p + 0x0C);
        s.rawSize = Read32(p + 0x10);
        s.nameAddress = Read32(p + 0x14);
        s.nameReferenceCount = Read32(p + 0x18);
        s.headSharedPageReferenceCountAddress = Read32(p + 0x1C);
        s.tailSharedPageReferenceCountAddress = Read32(p + 0x20);

        if (s.virtualSize == 0 || s.rawSize > s.virtualSize ||
            s.virtualAddress < parsed.baseAddress ||
            AddOverflows(s.virtualAddress, s.virtualSize) ||
            s.virtualAddress + s.virtualSize > parsed.baseAddress + parsed.imageSize ||
            s.rawAddress < parsed.headerSize ||
            !RangeInside(s.rawAddress, s.rawSize, fileSize))
            return {Error::InvalidSection, i};
    }

    // Reject virtual overlap, including sections that would overwrite headers.
    for (std::uint32_t i = 0; i < parsed.sectionCount; ++i) {
        const Section& a = parsed.sections[i];
        const std::uint32_t aStart = a.virtualAddress - parsed.baseAddress;
        const std::uint32_t aEnd = aStart + a.virtualSize;
        if (aStart < parsed.headerSize)
            return {Error::InvalidSection, i};
        for (std::uint32_t j = i + 1; j < parsed.sectionCount; ++j) {
            const Section& b = parsed.sections[j];
            const std::uint32_t bStart = b.virtualAddress - parsed.baseAddress;
            const std::uint32_t bEnd = bStart + b.virtualSize;
            if (aStart < bEnd && bStart < aEnd)
                return {Error::OverlappingSections, j};
        }
    }

    const auto entryInExecutableSection = [&parsed](std::uint32_t entry) noexcept {
        for (std::uint32_t i = 0; i < parsed.sectionCount; ++i) {
            const Section& s = parsed.sections[i];
            if ((s.flags & 0x04u) == 0) continue; // SECTION_EXECUTABLE
            if (entry >= s.virtualAddress &&
                static_cast<std::uint64_t>(entry) <
                    static_cast<std::uint64_t>(s.virtualAddress) + s.virtualSize)
                return true;
        }
        return false;
    };

    const bool retailValid = entryInExecutableSection(retailEntry);
    const bool debugValid = entryInExecutableSection(debugEntry);
    if (!retailValid && !debugValid)
        return {Error::InvalidEntryPoint, encodedEntry};

    parsed.debugImage = !retailValid && debugValid;
    parsed.entryPoint = parsed.debugImage ? debugEntry : retailEntry;
    parsed.kernelThunkAddress = parsed.debugImage ? debugThunk : retailThunk;

    if (parsed.kernelThunkAddress != 0) {
        if ((parsed.kernelThunkAddress & 3u) != 0 ||
            parsed.kernelThunkAddress < parsed.baseAddress ||
            static_cast<std::uint64_t>(parsed.kernelThunkAddress) + 4u >
                static_cast<std::uint64_t>(parsed.baseAddress) + parsed.imageSize)
            return {Error::InvalidThunkTable, parsed.kernelThunkAddress};
    }

    *output = parsed;
    return {};
}

Result MapImage(const std::uint8_t* file, std::size_t fileSize,
                const Image& image, std::uint8_t* destination,
                std::size_t destinationSize) noexcept {
    if (!file || !destination) return {Error::NullArgument, 0};
    if (image.imageSize == 0 || image.headerSize > fileSize ||
        image.imageSize > destinationSize)
        return {Error::OutputTooSmall, image.imageSize};

    std::memset(destination, 0, image.imageSize);
    std::memcpy(destination, file, image.headerSize);

    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const Section& s = image.sections[i];
        const std::size_t destinationOffset = s.virtualAddress - image.baseAddress;
        if (!RangeInside(s.rawAddress, s.rawSize, fileSize) ||
            !RangeInside(destinationOffset, s.virtualSize, image.imageSize) ||
            s.rawSize > s.virtualSize)
            return {Error::InvalidSection, i};
        if (s.rawSize)
            std::memcpy(destination + destinationOffset,
                        file + s.rawAddress, s.rawSize);
        // Remaining virtualSize bytes stay zero-filled.
    }
    return {};
}

const char* ErrorName(Error error) noexcept {
    switch (error) {
    case Error::None: return "none";
    case Error::NullArgument: return "null argument";
    case Error::FileTooSmall: return "file too small";
    case Error::BadMagic: return "invalid XBE magic";
    case Error::InvalidHeaderSize: return "invalid header size";
    case Error::InvalidImageSize: return "invalid image size";
    case Error::AddressOverflow: return "address overflow";
    case Error::InvalidSectionCount: return "invalid section count";
    case Error::SectionTableOutOfRange: return "section table out of range";
    case Error::InvalidCertificate: return "invalid certificate";
    case Error::InvalidSection: return "invalid section";
    case Error::OverlappingSections: return "overlapping sections";
    case Error::InvalidEntryPoint: return "entry point is not executable";
    case Error::InvalidThunkTable: return "invalid kernel thunk table address";
    case Error::OutputTooSmall: return "destination image buffer too small";
    }
    return "unknown XBE error";
}
} // namespace XboxXbe
