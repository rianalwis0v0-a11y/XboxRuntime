#pragma once
#include <windows.h>
#include <cstdint>

struct XboxKernelState {
    uint32_t imageBase;
    uint32_t imageSize;
    uint32_t entryPoint;
    uint32_t sectionCount;
    uint32_t tlsAddress;
};
bool XboxKernelResolveImports(const XboxKernelState& image);
DWORD WINAPI XboxKernelStartXbe(void* context);
