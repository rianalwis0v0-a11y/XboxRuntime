#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>

// Pinned XboxDev/nxdk xboxkrnl.h: PS_STATISTICS is three ULONGs.
// ThreadCount/HandleCount currently describe the isolated x86 guest process'
// Windows backing process, not a console-wide Xbox object-manager snapshot.
struct XboxPsStatistics {
    ULONG Length;
    ULONG ThreadCount;
    ULONG HandleCount;
};
static_assert(sizeof(XboxPsStatistics) == 12, "NXDK PS_STATISTICS must be 12 bytes");

extern "C" LONG __stdcall K_PsQueryStatistics(XboxPsStatistics* statistics);

struct XboxObjectString {
    USHORT Length;
    USHORT MaximumLength;
    char* Buffer;
};
static_assert(sizeof(XboxObjectString) == (sizeof(void*) == 4 ? 8 : 16), "OBJECT_STRING layout must match native pointer alignment (x86 guest is 8 bytes)");

extern "C" LONG __stdcall K_IoCreateSymbolicLink(
    XboxObjectString* symbolicLinkName, XboxObjectString* deviceName);
extern "C" LONG __stdcall K_IoDeleteSymbolicLink(XboxObjectString* symbolicLinkName);

// Resolves the guest object-manager link prefix in-place. Returns false for cycles
// or excessive link depth; true means the path is resolved or has no matching link.
bool XboxKernelResolveSymbolicLinks(std::wstring* path);

struct XboxListEntry {
    XboxListEntry* Flink;
    XboxListEntry* Blink;
};
struct XboxHalShutdownRegistration;
using XboxHalShutdownNotification = void (__stdcall*)(XboxHalShutdownRegistration*);
struct XboxHalShutdownRegistration {
    XboxHalShutdownNotification NotificationRoutine;
    LONG Priority;
    XboxListEntry ListEntry;
};
static_assert(sizeof(XboxHalShutdownRegistration) == (sizeof(void*) == 4 ? 16 : 32),
              "HAL_SHUTDOWN_REGISTRATION layout mismatch");
static_assert(offsetof(XboxHalShutdownRegistration, ListEntry) ==
                  (sizeof(void*) == 4 ? 8 : 16),
              "HAL_SHUTDOWN_REGISTRATION list offset mismatch");

extern "C" void __stdcall K_HalRegisterShutdownNotification(
    XboxHalShutdownRegistration* registration, BOOLEAN registerNotification);
void XboxKernelInvokeShutdownNotifications();
void XboxKernelResetShutdownNotifications();
