#pragma once
#include <cstdint>

constexpr std::uint32_t XBOX_GUEST_PROTOCOL_MAGIC = 0x47584258u;
constexpr std::uint32_t XBOX_GUEST_PROTOCOL_VERSION = 1u;

enum XboxGuestCommand : std::uint32_t {
    XboxGuestResolveImports = 1u,
    XboxGuestStart = 2u,
    XboxGuestStop = 3u,
    XboxGuestWaitForExit = 4u,
};

struct XboxGuestHello {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t status;
    std::uint32_t imageBase;
    std::uint32_t imageSize;
};

struct XboxGuestCommandMessage {
    std::uint32_t magic;
    std::uint32_t command;
    std::uint32_t imageBase;
    std::uint32_t imageSize;
    std::uint32_t headerSize;
    std::uint32_t entryPoint;
    std::uint32_t tlsAddress;
    std::uint32_t sectionCount;
    std::uint32_t nonKernelImportDirectoryAddress;
    std::uint32_t kernelThunkAddress;
};

struct XboxGuestCommandReply {
    std::uint32_t magic;
    std::uint32_t command;
    std::uint32_t status;
};

struct XboxGuestExitReply {
    std::uint32_t magic;
    std::uint32_t command;
    std::uint32_t status;
    std::uint32_t exitCode;
    std::uint32_t tlsIndexCleared;
};

static_assert(sizeof(XboxGuestHello) == 20, "guest hello wire size");
static_assert(sizeof(XboxGuestCommandMessage) == 40, "guest command wire size");
static_assert(sizeof(XboxGuestCommandReply) == 12, "guest reply wire size");
static_assert(sizeof(XboxGuestExitReply) == 20, "guest exit reply wire size");
