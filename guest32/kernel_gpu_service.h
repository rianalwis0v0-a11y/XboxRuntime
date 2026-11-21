#pragma once
#include <windows.h>
#include <stdint.h>

// The pipe is owned by guest32.cpp and remains valid until guest shutdown.
BOOL XboxKernelGpuServiceInitialize(HANDLE pipe);
void XboxKernelGpuServiceShutdown(void);
BOOL XboxKernelGpuServiceConnected(void);
BOOL XboxKernelGpuReadRegister(uint32_t offset, uint32_t width, uint64_t* value);
BOOL XboxKernelGpuWriteRegister(uint32_t offset, uint32_t width, uint64_t value);
BOOL XboxKernelGpuSubmitMethod(uint32_t subchannel, uint32_t method, uint32_t parameter);
BOOL XboxKernelGpuFlush(void);
BOOL XboxKernelGpuReadVram(uint32_t offset, void* destination, uint32_t length);
// Read a larger VRAM span by splitting it into bounded RPC transfers.
BOOL XboxKernelGpuReadVramRange(uint32_t offset, void* destination, uint32_t length);
BOOL XboxKernelGpuWriteVram(uint32_t offset, const void* source, uint32_t length);
BOOL XboxKernelGpuReadRamin(uint32_t offset, void* destination, uint32_t length);
BOOL XboxKernelGpuWriteRamin(uint32_t offset, const void* source, uint32_t length);

#ifdef __cplusplus
extern "C" {
#endif
uint64_t XboxKernelGpuReadRegisterValue(uint32_t offset, uint32_t width, int* succeeded);
BOOL XboxKernelGpuWriteRegisterValue(uint32_t offset, uint32_t width, uint64_t value);
BOOL XboxKernelGpuSubmitMethodValue(uint32_t subchannel, uint32_t method, uint32_t parameter);
BOOL XboxKernelGpuFlushValue(void);
BOOL XboxKernelGpuReadLocalMemory(BOOL ramin, uint32_t offset, void* destination, uint32_t length);
BOOL XboxKernelGpuWriteLocalMemory(BOOL ramin, uint32_t offset, const void* source, uint32_t length);
#ifdef __cplusplus
}
#endif
