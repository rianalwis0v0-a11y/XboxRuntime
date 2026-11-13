#pragma once
#include <windows.h>
#include <cstdint>

struct XboxKernelState {
    uint32_t imageBase;
    uint32_t imageSize;
    uint32_t headerSize;
    uint32_t entryPoint;
    uint32_t sectionCount;
    uint32_t tlsAddress;
    uint32_t nonKernelImportDirectoryAddress;
    uint32_t kernelThunkAddress;
};
bool XboxKernelInitializeEeprom(const wchar_t* path);
void XboxKernelFlushEeprom();
void XboxKernelSetTitlePath(const wchar_t* path);
bool XboxKernelResolveImports(const XboxKernelState& image);
bool XboxKernelProtectImage();
void XboxKernelSetStartupEvent(HANDLE eventHandle);
void XboxKernelShutdownTitle();
DWORD WINAPI XboxKernelStartXbe(void* context);
void XboxKernelGetLastExceptionDiagnostic(std::uint32_t* code,
                                              std::uint32_t* instruction,
                                              std::uint32_t* accessType,
                                              std::uint32_t* faultAddress,
                                              std::uint32_t* faultProtection,
                                              std::uint32_t* faultState);
