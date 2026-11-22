#include "static_xnet_trace.h"
#include <cstdio>

int main() {
    const XboxStaticXnetRange ranges[] = {
        {0x00120000u, 0x00001000u},
        {0x00200000u, 0x00000200u}
    };
    if (!XboxStaticXnetContains(ranges, 2, 0x00120000u)) return 1;
    if (!XboxStaticXnetContains(ranges, 2, 0x00120FFFu)) return 2;
    if (XboxStaticXnetContains(ranges, 2, 0x00121000u)) return 3;
    if (!XboxStaticXnetContains(ranges, 2, 0x002001FFu)) return 4;
    if (XboxStaticXnetContains(ranges, 2, 0x00200200u)) return 5;
    if (XboxStaticXnetContains(nullptr, 0, 0x00120000u)) return 6;
    if (!XboxStaticXnetKernelCandidateName(202)) return 7;
    if (!XboxStaticXnetKernelCandidateName(219)) return 8;
    if (!XboxStaticXnetKernelCandidateName(236)) return 9;
    if (!XboxStaticXnetKernelCandidateName(86)) return 10;
    if (XboxStaticXnetKernelCandidateName(201)) return 11;
    std::puts("static_xnet_trace_smoke: section range and kernel candidate classification passed");
    return 0;
}
