#pragma once
#include <cstdint>

struct XboxStaticXnetRange {
    std::uint32_t begin;
    std::uint32_t size;
};

inline bool XboxStaticXnetContains(const XboxStaticXnetRange* ranges,
                                   std::uint32_t count,
                                   std::uint32_t address) noexcept {
    if (!ranges) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t end = std::uint64_t(ranges[i].begin) + ranges[i].size;
        if (ranges[i].size && address >= ranges[i].begin &&
            std::uint64_t(address) < end) return true;
    }
    return false;
}

inline const char* XboxStaticXnetKernelCandidateName(std::uint32_t ordinal) noexcept {
    switch (ordinal) {
    case 59: return "IoAllocateIrp";
    case 60: return "IoBuildAsynchronousFsdRequest";
    case 61: return "IoBuildDeviceIoControlRequest";
    case 62: return "IoBuildSynchronousFsdRequest";
    case 65: return "IoCreateDevice";
    case 66: return "IoCreateFile";
    case 67: return "IoCreateSymbolicLink";
    case 68: return "IoDeleteDevice";
    case 69: return "IoDeleteSymbolicLink";
    case 72: return "IoFreeIrp";
    case 73: return "IoInitializeIrp";
    case 77: return "IoQueueThreadIrp";
    case 81: return "IoStartNextPacket";
    case 82: return "IoStartNextPacketByKey";
    case 83: return "IoStartPacket";
    case 84: return "IoSynchronousDeviceIoControlRequest";
    case 85: return "IoSynchronousFsdRequest";
    case 86: return "IofCallDriver";
    case 87: return "IofCompleteRequest";
    case 187: return "NtClose";
    case 190: return "NtCreateFile";
    case 196: return "NtDeviceIoControlFile";
    case 198: return "NtFlushBuffersFile";
    case 200: return "NtFsControlFile";
    case 202: return "NtOpenFile";
    case 207: return "NtQueryDirectoryFile";
    case 211: return "NtQueryInformationFile";
    case 218: return "NtQueryVolumeInformationFile";
    case 219: return "NtReadFile";
    case 236: return "NtWriteFile";
    case 252: return "PhyGetLinkState";
    case 253: return "PhyInitialize";
    default: return nullptr;
    }
}
