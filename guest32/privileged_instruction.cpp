#include "privileged_instruction.h"

namespace {
constexpr std::uint32_t kIa32EblCrPowerOn = 0x2Au;
// Xbox's Pentium III runs at 733 MHz on a 133 MHz FSB (5.5x).
// Pentium III MSR 0x2A encodes multiplier 5.5 as 0x04 in bits [27,25:22]
// and a 133 MHz FSB as 0x01 in bits [19:18].
constexpr std::uint32_t kXboxEblCrPowerOnLow = 0x01040000u;
}

bool XboxGuestEmulateRdmsr(CONTEXT* context,
                           const std::uint8_t* instructionBytes,
                           std::size_t availableBytes) {
    if (!context || !instructionBytes || availableBytes < 2 ||
        instructionBytes[0] != 0x0Fu || instructionBytes[1] != 0x32u ||
        context->Ecx != kIa32EblCrPowerOn) {
        return false;
    }

    context->Eax = kXboxEblCrPowerOnLow;
    context->Edx = 0;
    context->Eip += 2;
    return true;
}
