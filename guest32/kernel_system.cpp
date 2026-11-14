#include "kernel_system.h"
#include <tlhelp32.h>

namespace {
constexpr LONG kStatusSuccess = 0;
constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004u);
constexpr LONG kStatusUnsuccessful = static_cast<LONG>(0xC0000001u);
}

extern "C" LONG __stdcall K_PsQueryStatistics(XboxPsStatistics* statistics) {
    if (!statistics) return kStatusInvalidParameter;
    if (statistics->Length < sizeof(XboxPsStatistics))
        return kStatusInfoLengthMismatch;

    const DWORD processId = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return kStatusUnsuccessful;

    DWORD threadCount = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL ok = Thread32First(snapshot, &entry);
    if (!ok && GetLastError() != ERROR_NO_MORE_FILES) {
        CloseHandle(snapshot);
        return kStatusUnsuccessful;
    }
    while (ok) {
        if (entry.th32OwnerProcessID == processId) {
            if (threadCount == MAXDWORD) {
                CloseHandle(snapshot);
                return kStatusUnsuccessful;
            }
            ++threadCount;
        }
        entry.dwSize = sizeof(entry);
        ok = Thread32Next(snapshot, &entry);
    }
    const DWORD iterationError = GetLastError();
    CloseHandle(snapshot);
    if (iterationError != ERROR_NO_MORE_FILES) return kStatusUnsuccessful;

    DWORD handleCount = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &handleCount))
        return kStatusUnsuccessful;

    // Commit output only after both host queries succeed.
    XboxPsStatistics result{};
    result.Length = sizeof(result);
    result.ThreadCount = threadCount;
    result.HandleCount = handleCount;
    *statistics = result;
    return kStatusSuccess;
}
