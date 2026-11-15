#pragma once
#include <windows.h>
#include <cstdint>

using XboxKernelNormalRoutine = void (__stdcall*)(void*, void*, void*);

extern "C" {
void __fastcall K_HalClearSoftwareInterrupt(UCHAR requestIrql);
void __fastcall K_HalRequestSoftwareInterrupt(UCHAR requestIrql);
void __stdcall K_HalDisableSystemInterrupt(ULONG busInterruptLevel);
void __stdcall K_HalEnableSystemInterrupt(ULONG busInterruptLevel, ULONG interruptMode);
ULONG __stdcall K_HalGetInterruptVector(ULONG busInterruptLevel, UCHAR* irql);
void __stdcall K_HalReadWritePCISpace(ULONG busNumber, ULONG slotNumber,
    ULONG registerNumber, void* buffer, ULONG length, BOOLEAN writePciSpace);
LONG __stdcall K_KeSaveFloatingPointState(void* floatSave);
LONG __stdcall K_KeRestoreFloatingPointState(void* floatSave);
BOOLEAN __stdcall K_KeAlertThread(HANDLE thread, UCHAR processorMode);
void __stdcall K_KeInitializeApc(void* apc, void* thread,
    void (__stdcall* kernelRoutine)(void*, XboxKernelNormalRoutine*, void**, void**, void**),
    void (__stdcall* rundownRoutine)(void*),
    XboxKernelNormalRoutine normalRoutine,
    UCHAR processorMode, void* normalContext);
BOOLEAN __stdcall K_KeInsertQueueApc(void* apc, void* systemArgument1,
                                      void* systemArgument2, LONG increment);
BOOLEAN __stdcall K_KeConnectInterrupt(void* interrupt);
BOOLEAN __stdcall K_KeDisconnectInterrupt(void* interrupt);
void __stdcall K_KeInitializeInterrupt(void* interrupt,
    BOOLEAN (__stdcall* serviceRoutine)(void*, void*), void* serviceContext,
    ULONG vector, UCHAR irql, ULONG interruptMode, BOOLEAN shareVector);
BOOLEAN __stdcall K_KeSynchronizeExecution(void* interrupt,
    BOOLEAN (__stdcall* synchronizeRoutine)(void*), void* synchronizeContext);
}

void XboxKernelResetThreadExports();

UCHAR XboxKernelGetPendingSoftwareIrql();
