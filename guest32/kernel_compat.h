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
bool XboxKernelInitializeEeprom(const wchar_t* path);
void XboxKernelFlushEeprom();
void XboxKernelSetTitlePath(const wchar_t* path);
void XboxKernelResetStaticXnetTrace();
void XboxKernelAddStaticXnetCodeRange(uint32_t address, uint32_t size, uint32_t libraryBuild);
uint32_t XboxKernelGetStaticXnetTraceCount();
bool XboxKernelResolveImports(const XboxKernelState& image);
DWORD WINAPI XboxKernelStartXbe(void* context);
