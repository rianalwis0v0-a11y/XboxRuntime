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
bool XboxKernelResolveImports(const XboxKernelState& image);
DWORD WINAPI XboxKernelStartXbe(void* context);

// Guest-process GPU service surface. Kernel compatibility implementations use
// these routines rather than dereferencing host-process pointers or MMIO aliases.
#include "nv2a_guest_rpc.h"

/* Kernel-facing NV2A service facade. These operations cross the guest/host
 * process boundary through the versioned RPC transport. */
#ifdef __cplusplus
extern "C" {
#endif
uint64_t XboxKernelGpuReadRegister(uint32_t offset, uint32_t accessSize, int* ok);
int XboxKernelGpuWriteRegister(uint32_t offset, uint64_t value, uint32_t accessSize);
int XboxKernelGpuSubmitMethod(uint32_t subchannel, uint32_t method, uint32_t parameter);
int XboxKernelGpuFlush(void);
#ifdef __cplusplus
}
#endif
