#include "kernel_nvsettings.h"

#include <cstdint>
#include <cstdio>

int main() {
    XboxNvSettingInfo info{};
    if (!XboxNvGetSettingInfo(0x0007, &info) || info.offset != 0x90 ||
        info.length != 4 || info.type != 4 || !info.writable) {
        std::fprintf(stderr, "FAIL: XC_LANGUAGE descriptor\n");
        return 1;
    }
    if (!XboxNvGetSettingInfo(0x00FF, &info) || info.offset != 0x60 ||
        info.length != 0x60 || info.type != 3 || info.writable) {
        std::fprintf(stderr, "FAIL: XC_MAX_OS descriptor\n");
        return 1;
    }
    if (!XboxNvGetSettingInfo(0x01FF, &info) || info.offset != 0x30 ||
        info.length != 0x30 || info.type != 3 || info.writable) {
        std::fprintf(stderr, "FAIL: XC_MAX_FACTORY descriptor\n");
        return 1;
    }
    if (!XboxNvGetSettingInfo(0xFFFE, &info) || info.offset != 0 ||
        info.length != 0x30 || info.type != 3 || info.writable) {
        std::fprintf(stderr, "FAIL: XC_ENCRYPTED_SECTION descriptor\n");
        return 1;
    }
    if (!XboxNvGetSettingInfo(0xFFFF, &info) || info.offset != 0 ||
        info.length != 256 || info.type != 3 || info.writable) {
        std::fprintf(stderr, "FAIL: XC_MAX_ALL descriptor\n");
        return 1;
    }
    if (XboxNvGetSettingInfo(0x7777, &info)) {
        std::fprintf(stderr, "FAIL: unknown index accepted\n");
        return 1;
    }
    const std::uint8_t zeroWord[4]{};
    const std::uint8_t oneWord[4]{1, 0, 0, 0};
    const std::uint8_t carryVector[12] = {0xFF,0xFF,0xFF,0xFF, 1,0,0,0, 0xFF,0xFF,0xFF,0xFF};
    if (XboxNvComputeChecksum(zeroWord, sizeof(zeroWord)) != 0xFFFFFFFFu ||
        XboxNvComputeChecksum(oneWord, sizeof(oneWord)) != 0xFFFFFFFEu ||
        XboxNvComputeChecksum(carryVector, sizeof(carryVector)) != 0xFFFFFFFEu) {
        std::fprintf(stderr, "FAIL: XConfigChecksum vectors\n");
        return 1;
    }
    std::puts("PASS: Group A NV setting descriptors and XConfigChecksum host vectors");
    return 0;
}
