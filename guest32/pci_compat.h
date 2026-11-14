#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void __stdcall K_HalReadWritePCISpace(
    ULONG busNumber,
    ULONG slotNumber,
    ULONG registerNumber,
    PVOID buffer,
    ULONG length,
    BOOLEAN writePciSpace);
