#pragma once
#include <windows.h>
#include <cstdint>

// Pinned XboxDev/nxdk xboxkrnl.h: PS_STATISTICS is three ULONGs.
// ThreadCount/HandleCount currently describe the isolated x86 guest process'
// Windows backing process, not a console-wide Xbox object-manager snapshot.
struct XboxPsStatistics {
    ULONG Length;
    ULONG ThreadCount;
    ULONG HandleCount;
};
static_assert(sizeof(XboxPsStatistics) == 12, "NXDK PS_STATISTICS must be 12 bytes");

extern "C" LONG __stdcall K_PsQueryStatistics(XboxPsStatistics* statistics);
