#pragma once
#include <windows.h>
#include <cstdint>

extern "C" LONG __stdcall K_NtSetSystemTime(LARGE_INTEGER* systemTime, LARGE_INTEGER* previousTime);
extern "C" void __stdcall K_KeQuerySystemTime(LARGE_INTEGER* currentTime);
std::int64_t XboxKernelCurrentSystemTime100ns();
void XboxKernelResetSystemTime();
