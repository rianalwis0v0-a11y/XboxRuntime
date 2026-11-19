#pragma once
#include <windows.h>
#include <stdint.h>

// Guest-process client for the versioned host NV2A RPC protocol.
// The pipe is owned by guest32.cpp and remains valid until guest shutdown.
void XboxGuestNV2ASetPipe(HANDLE pipe);
uint64_t XboxGuestNV2AReadRegister(uint32_t offset, uint32_t accessSize, int* ok);
int XboxGuestNV2AWriteRegister(uint32_t offset, uint64_t value, uint32_t accessSize);
int XboxGuestNV2ASubmitMethod(uint32_t subchannel, uint32_t method, uint32_t parameter);
int XboxGuestNV2AFlush(void);
