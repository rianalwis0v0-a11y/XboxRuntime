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

bool ReadVirtualByte(const std::uint8_t* file, std::size_t fileSize,
                      const Image& image, std::uint32_t address,
                      std::uint8_t* value) noexcept {
    if (!value || address < image.baseAddress ||
        static_cast<std::uint64_t>(address) >=
            static_cast<std::uint64_t>(image.baseAddress) + image.imageSize)
        return false;

    const std::uint32_t imageOffset = address - image.baseAddress;
    if (imageOffset < image.headerSize) {
        if (!RangeInside(imageOffset, 1, fileSize)) return false;
        *value = file[imageOffset];
        return true;
    }

    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const Section& section = image.sections[i];
        if (address < section.virtualAddress ||
            static_cast<std::uint64_t>(address) >=
                static_cast<std::uint64_t>(section.virtualAddress) +
                    section.virtualSize)
            continue;

        const std::uint32_t sectionOffset = address - section.virtualAddress;
        if (sectionOffset >= section.rawSize) {
            *value = 0; // virtual zero-fill
            return true;
        }
        const std::size_t rawOffset =
            static_cast<std::size_t>(section.rawAddress) + sectionOffset;
        if (!RangeInside(rawOffset, 1, fileSize)) return false;
        *value = file[rawOffset];
        return true;
    }
    return false;
}

bool ReadVirtual32(const std::uint8_t* file, std::size_t fileSize,
                   const Image& image, std::uint32_t address,
                   std::uint32_t* value) noexcept {
    if (!value || address > (std::numeric_limits<std::uint32_t>::max)() - 3u)
        return false;
    std::uint8_t bytes[4]{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        if (!ReadVirtualByte(file, fileSize, image, address + i, &bytes[i]))
            return false;
    }
    *value = Read32(bytes);
    return true;
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

bool IsVirtualRangeMapped(const Image& image, std::uint32_t address,
                          std::uint32_t length) noexcept {
    const std::uint64_t start = address;
    const std::uint64_t end = start + length;
    const std::uint64_t imageEnd =
        static_cast<std::uint64_t>(image.baseAddress) + image.imageSize;
    if (start < image.baseAddress || end > imageEnd || end < start)
        return false;
    if (start >= image.baseAddress &&
        end <= static_cast<std::uint64_t>(image.baseAddress) + image.headerSize)
        return true;
    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const Section& section = image.sections[i];
        const std::uint64_t sectionStart = section.virtualAddress;
        const std::uint64_t sectionEnd = sectionStart + section.virtualSize;
        if (start >= sectionStart && end <= sectionEnd)
            return true;
    }
    return length == 0;
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
    parsed.tlsAddress = Read32(file + 0x12C);
    parsed.certificateAddress = Read32(file + 0x118);
    parsed.nonKernelImportDirectoryAddress = Read32(file + 0x15C);
    parsed.libraryVersionCount = Read32(file + 0x160);
    parsed.libraryVersionsAddress = Read32(file + 0x164);
    parsed.kernelLibraryVersionAddress = Read32(file + 0x168);
    parsed.xapiLibraryVersionAddress = Read32(file + 0x16C);
    parsed.sectionCount = Read32(file + 0x11C);
    const std::uint32_t sectionTableAddress = Read32(file + 0x120);
    const std::uint32_t encodedEntry = Read32(file + 0x128);
    const std::uint32_t encodedThunk = Read32(file + 0x158);

    if (parsed.imageHeaderSize < kMinimumHeaderSize ||
        parsed.imageHeaderSize > parsed.headerSize ||
        parsed.headerSize > fileSize)
        return {Error::InvalidHeaderSize, parsed.headerSize};

    if (parsed.imageSize < parsed.headerSize ||
        parsed.imageSize > kMaximumImageSize ||
        parsed.baseAddress == 0 ||
        AddOverflows(parsed.baseAddress, parsed.imageSize))
        return {Error::InvalidImageSize, parsed.imageSize};

    if (parsed.libraryVersionCount > 128u)
        return {Error::InvalidLibraryVersions, parsed.libraryVersionCount};
    if (parsed.libraryVersionCount != 0) {
        const std::uint64_t libraryBytes =
            static_cast<std::uint64_t>(parsed.libraryVersionCount) * 16u;
        if (libraryBytes > (std::numeric_limits<std::size_t>::max)() ||
            !VaRangeToFile(parsed.libraryVersionsAddress, parsed.baseAddress,
                           parsed.headerSize, static_cast<std::size_t>(libraryBytes),
                           fileSize, nullptr))
            return {Error::InvalidLibraryVersions, parsed.libraryVersionsAddress};
    }
    const auto validLibraryVersionPointer = [&](std::uint32_t address) noexcept {
        return address == 0 ||
            (parsed.libraryVersionCount != 0 &&
             VaRangeToFile(address, parsed.baseAddress, parsed.headerSize,
                           16u, fileSize, nullptr));
    };
    if (!validLibraryVersionPointer(parsed.kernelLibraryVersionAddress) ||
        !validLibraryVersionPointer(parsed.xapiLibraryVersionAddress))
        return {Error::InvalidLibraryVersions, parsed.libraryVersionsAddress};

    if (parsed.nonKernelImportDirectoryAddress != 0 &&
        (parsed.nonKernelImportDirectoryAddress < parsed.baseAddress ||
         static_cast<std::uint64_t>(parsed.nonKernelImportDirectoryAddress) + 4u >
             static_cast<std::uint64_t>(parsed.baseAddress) + parsed.imageSize))
        return {Error::InvalidImportDirectory, parsed.nonKernelImportDirectoryAddress};

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
    if (certificateSize < kMinimumCertificateSize ||
        !VaRangeToFile(parsed.certificateAddress, parsed.baseAddress,
                       parsed.headerSize, certificateSize, fileSize, nullptr))
        return {Error::InvalidCertificate, certificateSize};

    // Match Cxbx-Reloaded's XBE type selection before decoding either
    // address. The entry-point value identifies Chihiro; otherwise the
    // encoded thunk's KSEG0 bit distinguishes debug from retail XBEs.
    // Do not choose an XOR key merely because its decoded address happens
    // to land inside an executable section: that can misclassify malformed
    // or deliberately ambiguous headers.
    parsed.chihiroImage =
        (encodedEntry & kChihiroEntryTypeMask) == kChihiroEntryTypeValue;
    parsed.debugImage = !parsed.chihiroImage &&
        (encodedThunk & kDebugThunkTypeMask) != 0;

    const std::uint32_t entryXor = parsed.chihiroImage
        ? kChihiroEntryXor
        : (parsed.debugImage ? kDebugEntryXor : kRetailEntryXor);
    const std::uint32_t thunkXor = parsed.chihiroImage
        ? kChihiroThunkXor
        : (parsed.debugImage ? kDebugThunkXor : kRetailThunkXor);
    const std::uint32_t decodedEntry = encodedEntry ^ entryXor;
    const std::uint32_t decodedThunk = encodedThunk ^ thunkXor;

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

        if (s.nameAddress != 0) {
            std::size_t nameOffset = 0;
            if (!VaRangeToFile(s.nameAddress, parsed.baseAddress,
                               parsed.headerSize, 1u, fileSize, &nameOffset))
                return {Error::InvalidSection, i};
            const std::size_t nameLimit =
                (std::min)(static_cast<std::size_t>(32u),
                           static_cast<std::size_t>(parsed.headerSize) - nameOffset);
            if (std::memchr(file + nameOffset, 0, nameLimit) == nullptr)
                return {Error::InvalidSection, i};
        }

        if (s.virtualSize == 0 || s.rawSize > s.virtualSize ||
            s.virtualAddress < parsed.baseAddress ||
            AddOverflows(s.virtualAddress, s.virtualSize) ||
            s.virtualAddress + s.virtualSize > parsed.baseAddress + parsed.imageSize ||
            (s.rawSize != 0 && s.rawAddress < parsed.headerSize) ||
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

    if (!entryInExecutableSection(decodedEntry))
        return {Error::InvalidEntryPoint, encodedEntry};

    parsed.entryPoint = decodedEntry;
    parsed.kernelThunkAddress = decodedThunk;

    if (parsed.kernelThunkAddress != 0) {
        bool terminated = false;
        for (std::uint32_t i = 0; i < 4096u; ++i) {
            const std::uint64_t current =
                static_cast<std::uint64_t>(parsed.kernelThunkAddress) +
                static_cast<std::uint64_t>(i) * 4u;
            if (current > (std::numeric_limits<std::uint32_t>::max)())
                break;
            std::uint32_t thunk = 0;
            if (!ReadVirtual32(file, fileSize, parsed,
                               static_cast<std::uint32_t>(current), &thunk))
                break;
            if (thunk == 0) {
                terminated = true;
                break;
            }
        }
        if (!terminated)
            return {Error::InvalidThunkTable, parsed.kernelThunkAddress};
    }


    if (parsed.tlsAddress != 0) {
        std::uint32_t dataStart = 0;
        std::uint32_t dataEnd = 0;
        std::uint32_t indexAddress = 0;
        std::uint32_t callbackAddress = 0;
        std::uint32_t zeroFill = 0;
        std::uint32_t characteristics = 0;
        const bool readableDirectory =
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 0u, &dataStart) &&
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 4u, &dataEnd) &&
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 8u, &indexAddress) &&
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 12u, &callbackAddress) &&
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 16u, &zeroFill) &&
            ReadVirtual32(file, fileSize, parsed, parsed.tlsAddress + 20u, &characteristics);
        (void)zeroFill;
        (void)characteristics;
        if (!readableDirectory || dataEnd < dataStart ||
            (dataStart != dataEnd &&
             !IsVirtualRangeMapped(parsed, dataStart, dataEnd - dataStart)) ||
            (indexAddress != 0 &&
             !IsVirtualRangeMapped(parsed, indexAddress, sizeof(std::uint32_t))) ||
            (callbackAddress != 0 &&
             !IsVirtualRangeMapped(parsed, callbackAddress, sizeof(std::uint32_t))))
            return {Error::InvalidSection, parsed.tlsAddress};
    }

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
    if (image.sectionCount == 0 || image.sectionCount > kMaximumSections ||
        image.baseAddress == 0 || image.headerSize > image.imageSize ||
        image.imageSize > kMaximumImageSize ||
        AddOverflows(image.baseAddress, image.imageSize))
        return {Error::InvalidImageSize, image.imageSize};
    if (image.imageSize == 0 || image.headerSize < kMinimumHeaderSize ||
        image.headerSize > fileSize || image.imageSize > destinationSize)
        return {Error::OutputTooSmall, image.imageSize};

    // MapImage is public and may receive an Image not produced by Parse.
    // Validate all sections before touching the caller's destination buffer.
    const std::uint64_t imageEnd =
        static_cast<std::uint64_t>(image.baseAddress) + image.imageSize;
    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const Section& section = image.sections[i];
        if (section.virtualAddress < image.baseAddress ||
            section.virtualSize == 0 ||
            section.rawSize > section.virtualSize ||
            (section.rawSize != 0 && section.rawAddress < image.headerSize) ||
            !RangeInside(section.rawAddress, section.rawSize, fileSize))
            return {Error::InvalidSection, i};

        const std::uint64_t sectionStart =
            static_cast<std::uint64_t>(section.virtualAddress) - image.baseAddress;
        const std::uint64_t sectionEnd = sectionStart + section.virtualSize;
        if (sectionStart < image.headerSize || sectionEnd > image.imageSize ||
            static_cast<std::uint64_t>(section.virtualAddress) +
                section.virtualSize > imageEnd)
            return {Error::InvalidSection, i};

        for (std::uint32_t j = 0; j < i; ++j) {
            const Section& previous = image.sections[j];
            const std::uint64_t previousStart =
                static_cast<std::uint64_t>(previous.virtualAddress) -
                image.baseAddress;
            const std::uint64_t previousEnd =
                previousStart + previous.virtualSize;
            if (sectionStart < previousEnd && previousStart < sectionEnd)
                return {Error::OverlappingSections, i};
        }
    }

    std::memset(destination, 0, image.imageSize);
    std::memcpy(destination, file, image.headerSize);

    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const Section& s = image.sections[i];
        if (s.virtualAddress < image.baseAddress || s.virtualSize == 0 ||
            s.rawSize > s.virtualSize ||
            (s.rawSize != 0 && s.rawAddress < image.headerSize))
            return {Error::InvalidSection, i};
        const std::size_t destinationOffset = s.virtualAddress - image.baseAddress;
        if (!RangeInside(s.rawAddress, s.rawSize, fileSize) ||
            !RangeInside(destinationOffset, s.virtualSize, image.imageSize))
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
    case Error::InvalidLibraryVersions: return "invalid library versions";
    case Error::InvalidImportDirectory: return "invalid non-kernel import directory";
    case Error::InvalidSection: return "invalid section";
    case Error::OverlappingSections: return "overlapping sections";
    case Error::InvalidEntryPoint: return "entry point is not executable";
    case Error::InvalidThunkTable: return "invalid kernel thunk table";
    case Error::OutputTooSmall: return "destination image buffer too small";
    }
    return "unknown XBE error";
}
} // namespace XboxXbe
