#pragma once
#include <windows.h>

BOOL XboxKernelGpuMmioInitialize(void);
void XboxKernelGpuMmioShutdown(void);

// Exposed for the guest32 decoder smoke test; normal callers use VEH install.
LONG CALLBACK XboxKernelGpuMmioExceptionHandler(EXCEPTION_POINTERS* exception);
