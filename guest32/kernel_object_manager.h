#pragma once
#include <windows.h>
#include <cstdint>

// Guest object handles are 32-bit tokens, never native Win32 HANDLE values.
enum class XboxKernelObjectType : uint32_t {
    Event = 1, Semaphore, Mutant, Timer, IoCompletion, SymbolicLink, Directory
};
using XboxKernelObjectDestroy = void (*)(void* object);

enum class XboxKernelObjectLookupStatus : LONG {
    Success = 0,
    InvalidHandle = static_cast<LONG>(0xC0000008u),
    TypeMismatch = static_cast<LONG>(0xC0000024u),
    InvalidParameter = static_cast<LONG>(0xC000000Du),
    ShuttingDown = static_cast<LONG>(0xC0000184u)
};

// On failure, Create leaves payload ownership with the caller.
HANDLE XboxKernelCreateObjectHandle(void* object, XboxKernelObjectType type,
                                    XboxKernelObjectDestroy destroy);
// Every successful Acquire must be paired with exactly one Release.
// On failure, *object is always set to nullptr when object is non-null.
bool XboxKernelAcquireObjectReference(HANDLE handle, XboxKernelObjectType expectedType,
                                      void** object);
void XboxKernelReleaseObjectReference(void* object);
XboxKernelObjectLookupStatus XboxKernelGetLastObjectLookupStatus();

// Same object identity/state; only DUPLICATE_SAME_ACCESS (0x2) is accepted.
// Same-process guest handles only. *target is unchanged on failure.
LONG XboxKernelDuplicateObjectHandle(HANDLE source, HANDLE* target, ULONG options);
bool XboxKernelCloseObjectHandle(HANDLE handle);
bool XboxKernelIsObjectHandle(HANDLE handle);
// Blocks new creates/acquires and closes all published entries. Outstanding
// acquired references keep payloads alive until their matching Release.
void XboxKernelCloseAllObjectHandles();
// Reopens the table for a new title session after prior shutdown cleanup.
void XboxKernelBeginObjectHandleSession();
