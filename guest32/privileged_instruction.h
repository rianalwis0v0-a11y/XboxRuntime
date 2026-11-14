#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

// Emulate only the Xbox CPU MSR used by the NXDK runtime's Pentium III
// frequency probe. Unknown MSRs remain privileged-instruction faults.
bool XboxGuestEmulateRdmsr(CONTEXT* context,
                           const std::uint8_t* instructionBytes,
                           std::size_t availableBytes);
