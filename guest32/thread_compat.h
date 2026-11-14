#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>

// Xbox 1.0 KTHREAD layout (0x110 bytes). The prefix offsets used by NXDK are
// StackBase=0x1C, StackLimit=0x20, KernelStack=0x24, and TlsData=0x28.
struct XboxGuestKThread {
    std::uint8_t dispatcherHeader[0x10];
    LIST_ENTRY mutantListHead;
    ULONG kernelTime;
    PVOID stackBase;
    PVOID stackLimit;
    PVOID kernelStack;
    PVOID tlsData;
    UCHAR state;
    UCHAR alerted[2];
    UCHAR alertable;
    UCHAR npxState;
    CHAR saturation;
    CHAR priority;
    UCHAR padding;
    std::uint8_t reserved[0x110 - 0x34];
};

static_assert(offsetof(XboxGuestKThread, stackBase) == 0x1C, "Xbox KTHREAD StackBase offset");
static_assert(offsetof(XboxGuestKThread, stackLimit) == 0x20, "Xbox KTHREAD StackLimit offset");
static_assert(offsetof(XboxGuestKThread, tlsData) == 0x28, "Xbox KTHREAD TlsData offset");
static_assert(sizeof(XboxGuestKThread) == 0x110, "Xbox KTHREAD size");

using XboxGuestStartRoutine = VOID (NTAPI *)(PVOID);
using XboxGuestSystemRoutine = VOID (NTAPI *)(XboxGuestStartRoutine, PVOID);

extern "C" XboxGuestKThread* NTAPI K_KeGetCurrentThread();
extern "C" LONG NTAPI K_PsCreateSystemThreadEx(
    PHANDLE ThreadHandle,
    SIZE_T ThreadExtensionSize,
    SIZE_T KernelStackSize,
    SIZE_T TlsDataSize,
    PHANDLE ThreadId,
    XboxGuestStartRoutine StartRoutine,
    PVOID StartContext,
    BOOLEAN CreateSuspended,
    BOOLEAN DebuggerThread,
    XboxGuestSystemRoutine SystemRoutine);
extern "C" LONG NTAPI K_PsCreateSystemThread(
    PHANDLE ThreadHandle,
    PHANDLE ThreadId,
    XboxGuestStartRoutine StartRoutine,
    PVOID StartContext,
    BOOLEAN DebuggerThread);

void XboxKernelInitializeCurrentThread();
HANDLE XboxKernelResolveThreadHandle(HANDLE thread);
void XboxKernelForgetThreadHandle(HANDLE thread);
