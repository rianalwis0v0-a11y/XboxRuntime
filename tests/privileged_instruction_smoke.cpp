#include "../guest32/privileged_instruction.h"

#include <cstdint>
#include <cstdio>

int main() {
    const std::uint8_t rdmsr[] = {0x0F, 0x32};
    CONTEXT context{};
    context.Ecx = 0x2Au;
    context.Eip = 0x1000EBD6u;
    if (!XboxGuestEmulateRdmsr(&context, rdmsr, sizeof(rdmsr)) ||
        context.Eax != 0x01040000u || context.Edx != 0 ||
        context.Eip != 0x1000EBD8u) {
        std::fprintf(stderr, "FAIL: Xbox MSR 0x2A emulation result mismatch\n");
        return 1;
    }

    CONTEXT unsupported{};
    unsupported.Ecx = 0x10u;
    unsupported.Eip = 0x10001000u;
    if (XboxGuestEmulateRdmsr(&unsupported, rdmsr, sizeof(rdmsr)) ||
        unsupported.Eip != 0x10001000u || unsupported.Eax != 0 ||
        unsupported.Edx != 0) {
        std::fprintf(stderr, "FAIL: unsupported MSR was incorrectly emulated\n");
        return 2;
    }

    const std::uint8_t otherInstruction[] = {0x0F, 0x31}; // RDTSC, not RDMSR.
    CONTEXT other{};
    other.Ecx = 0x2Au;
    other.Eip = 0x10002000u;
    if (XboxGuestEmulateRdmsr(&other, otherInstruction, sizeof(otherInstruction)) ||
        other.Eip != 0x10002000u) {
        std::fprintf(stderr, "FAIL: non-RDMSR instruction was modified\n");
        return 3;
    }

    std::puts("PASS: Xbox Pentium III MSR 0x2A emulation and unsupported cases.");
    return 0;
}
