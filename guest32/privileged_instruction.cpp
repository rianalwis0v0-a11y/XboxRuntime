#include "privileged_instruction.h"

namespace {
constexpr std::uint32_t kIa32EblCrPowerOn = 0x2Au;
// Xbox's Pentium III runs at 733 MHz on a 133 MHz FSB (5.5x).
// MSR 0x2A encodes multiplier 5.5 and a 133 MHz front-side bus.
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
