#pragma once
#include <windows.h>
#include <cstdint>

extern "C" {
void __fastcall K_HalClearSoftwareInterrupt(UCHAR requestIrql);
void __fastcall K_HalRequestSoftwareInterrupt(UCHAR requestIrql);
BOOLEAN __stdcall K_KeAlertThread(HANDLE thread, UCHAR processorMode);
void __stdcall K_KeInitializeApc(void* apc, void* thread,
    void (__stdcall* kernelRoutine)(void*, void**, void**, void**, void**),
    void (__stdcall* rundownRoutine)(void*),
    void (__stdcall* normalRoutine)(void*, void*, void*),
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
