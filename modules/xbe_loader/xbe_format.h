#pragma once
#ifndef XBOX_RUNTIME_XBE_FORMAT_H
#define XBOX_RUNTIME_XBE_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace XboxXbe {

constexpr std::uint32_t kMagic = 0x48454258u; // "XBEH"
constexpr std::size_t kMinimumHeaderSize = 0x178;
constexpr std::size_t kMinimumCertificateSize = 0x0D0;
constexpr std::size_t kSectionHeaderSize = 0x38;
constexpr std::uint32_t kRetailEntryXor = 0xA8FC57ABu;
constexpr std::uint32_t kDebugEntryXor = 0x94859D4Bu;
constexpr std::uint32_t kChihiroEntryXor = 0x40B5C16Eu;
constexpr std::uint32_t kRetailThunkXor = 0x5B6D40B6u;
constexpr std::uint32_t kDebugThunkXor = 0xEFB1F152u;
constexpr std::uint32_t kChihiroThunkXor = 0x2290059Du;
constexpr std::uint32_t kChihiroEntryTypeMask = 0x40000000u;
constexpr std::uint32_t kChihiroEntryTypeValue = 0x40000000u;
constexpr std::uint32_t kDebugThunkTypeMask = 0x80000000u;
constexpr std::uint32_t kMaximumSections = 96;
constexpr std::uint32_t kMaximumImageSize = 512u * 1024u * 1024u;

enum class Error {
    None = 0,
    NullArgument,
    FileTooSmall,
    BadMagic,
    InvalidHeaderSize,
    InvalidImageSize,
    AddressOverflow,
    InvalidSectionCount,
    SectionTableOutOfRange,
    InvalidCertificate,
    InvalidLibraryVersions,
    InvalidImportDirectory,
    InvalidSection,
    OverlappingSections,
    InvalidEntryPoint,
    InvalidThunkTable,
    OutputTooSmall
};

struct Section {
    std::uint32_t flags;
    std::uint32_t virtualAddress;
    std::uint32_t virtualSize;
    std::uint32_t rawAddress;
    std::uint32_t rawSize;
    std::uint32_t nameAddress;
    std::uint32_t nameReferenceCount;
    std::uint32_t headSharedPageReferenceCountAddress;
    std::uint32_t tailSharedPageReferenceCountAddress;
};

struct Image {
    std::uint32_t baseAddress = 0;
    std::uint32_t headerSize = 0;
    std::uint32_t imageSize = 0;
    std::uint32_t imageHeaderSize = 0;
    std::uint32_t certificateAddress = 0;
    std::uint32_t nonKernelImportDirectoryAddress = 0;
    std::uint32_t libraryVersionCount = 0;
    std::uint32_t libraryVersionsAddress = 0;
    std::uint32_t kernelLibraryVersionAddress = 0;
    std::uint32_t xapiLibraryVersionAddress = 0;
    std::uint32_t sectionCount = 0;
    std::uint32_t entryPoint = 0;
    std::uint32_t tlsAddress = 0;
    std::uint32_t kernelThunkAddress = 0;
    bool debugImage = false;
    bool chihiroImage = false;
    Section sections[kMaximumSections]{};
};

struct Result {
    Error error = Error::None;
    std::uint32_t detail = 0;
    explicit operator bool() const noexcept { return error == Error::None; }
};

// Parses and validates file-backed XBE metadata without allocating guest memory.
Result Parse(const std::uint8_t* file, std::size_t fileSize, Image* output) noexcept;

// Creates the in-memory image layout. The caller owns the destination buffer.
// The destination must be at least Image::imageSize bytes.
Result MapImage(const std::uint8_t* file, std::size_t fileSize,
                const Image& image, std::uint8_t* destination,
                std::size_t destinationSize) noexcept;

// Converts an Xbox virtual address to a file offset within the loaded headers.
bool HeaderVaToFileOffset(std::uint32_t address, std::uint32_t baseAddress,
                          std::uint32_t headerSize, std::size_t length,
                          std::size_t* fileOffset) noexcept;

const char* ErrorName(Error error) noexcept;

} // namespace XboxXbe
#endif
