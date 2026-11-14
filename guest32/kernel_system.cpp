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

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <atomic>
#include <vector>

namespace {
std::mutex g_symbolicLinkMutex;
std::unordered_map<std::wstring, std::wstring> g_symbolicLinks;

std::wstring NormalizeObjectName(const std::wstring& value) {
    std::wstring normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return normalized;
}

bool ReadObjectString(const XboxObjectString* source, std::wstring* value) {
    if (!source || !value || !source->Buffer || !source->Length ||
        source->Length > source->MaximumLength) return false;
    const int characters = MultiByteToWideChar(
        CP_ACP, 0, source->Buffer, source->Length, nullptr, 0);
    if (characters <= 0) return false;
    std::wstring converted(static_cast<size_t>(characters), L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, source->Buffer, source->Length,
                            converted.data(), characters) != characters) return false;
    if (converted.find(L'\0') != std::wstring::npos ||
        (converted.front() != L'\\' && converted.front() != L'/')) return false;
    *value = std::move(converted);
    return true;
}
}

extern "C" LONG __stdcall K_IoCreateSymbolicLink(
    XboxObjectString* symbolicLinkName, XboxObjectString* deviceName) {
    constexpr LONG kIoStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusObjectNameCollision = static_cast<LONG>(0xC0000035u);
    std::wstring name, target;
    if (!ReadObjectString(symbolicLinkName, &name) ||
        !ReadObjectString(deviceName, &target)) return kIoStatusInvalidParameter;
    const std::wstring key = NormalizeObjectName(name);
    std::lock_guard<std::mutex> lock(g_symbolicLinkMutex);
    if (g_symbolicLinks.find(key) != g_symbolicLinks.end())
        return kStatusObjectNameCollision;
    g_symbolicLinks.emplace(key, std::move(target));
    return 0;
}

extern "C" LONG __stdcall K_IoDeleteSymbolicLink(XboxObjectString* symbolicLinkName) {
    constexpr LONG kIoStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusObjectNameNotFound = static_cast<LONG>(0xC0000034u);
    std::wstring name;
    if (!ReadObjectString(symbolicLinkName, &name)) return kStatusInvalidParameter;
    const std::wstring key = NormalizeObjectName(name);
    std::lock_guard<std::mutex> lock(g_symbolicLinkMutex);
    return g_symbolicLinks.erase(key) ? 0 : kStatusObjectNameNotFound;
}

bool XboxKernelResolveSymbolicLinks(std::wstring* path) {
    if (!path || path->empty()) return false;
    std::unordered_set<std::wstring> visited;
    constexpr size_t kMaximumLinkDepth = 16;
    for (size_t depth = 0; depth < kMaximumLinkDepth; ++depth) {
        std::wstring selectedKey, selectedTarget;
        {
            std::lock_guard<std::mutex> lock(g_symbolicLinkMutex);
            for (const auto& link : g_symbolicLinks) {
                const std::wstring& key = link.first;
                if (key.size() < selectedKey.size() || path->size() < key.size()) continue;
                const bool prefixMatches =
                    NormalizeObjectName(path->substr(0, key.size())) == key;
                const bool boundary = path->size() == key.size() ||
                    (*path)[key.size()] == L'\\' || (*path)[key.size()] == L'/';
                if (prefixMatches && boundary && key.size() > selectedKey.size()) {
                    selectedKey = key;
                    selectedTarget = link.second;
                }
            }
        }
        if (selectedKey.empty()) return true;
        if (!visited.insert(selectedKey).second) return false;
        path->replace(0, selectedKey.size(), selectedTarget);
    }
    // A chain still matching another link after the depth limit is rejected.
    std::lock_guard<std::mutex> lock(g_symbolicLinkMutex);
    for (const auto& link : g_symbolicLinks) {
        if (path->size() >= link.first.size() &&
            NormalizeObjectName(path->substr(0, link.first.size())) == link.first &&
            (path->size() == link.first.size() || (*path)[link.first.size()] == L'\\' ||
             (*path)[link.first.size()] == L'/')) return false;
    }
    return true;
}

namespace {
std::mutex g_shutdownNotificationMutex;
std::vector<XboxHalShutdownRegistration*> g_shutdownNotifications;
XboxListEntry g_shutdownNotificationHead{};
std::atomic<bool> g_shutdownNotificationsInvoked{false};

bool IsGuestRangeAccessible(const void* address, size_t length, bool requireWrite) {
    if (!address || !length) return false;
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (start > UINTPTR_MAX - length) return false;
    const uintptr_t end = start + length;
    uintptr_t cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const DWORD protection = info.Protect & 0xFFu;
        if (requireWrite && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (info.RegionSize > UINTPTR_MAX - regionBase) return false;
        const uintptr_t regionEnd = regionBase + info.RegionSize;
        if (regionEnd <= cursor) return false;
        cursor = regionEnd < end ? regionEnd : end;
    }
    return true;
}

bool IsExecutableGuestAddress(const void* address) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

void RebuildShutdownNotificationListLocked() {
    g_shutdownNotificationHead.Flink = &g_shutdownNotificationHead;
    g_shutdownNotificationHead.Blink = &g_shutdownNotificationHead;
    if (g_shutdownNotifications.empty()) return;
    for (size_t i = 0; i < g_shutdownNotifications.size(); ++i) {
        XboxListEntry* entry = &g_shutdownNotifications[i]->ListEntry;
        entry->Blink = i == 0 ? &g_shutdownNotificationHead
                              : &g_shutdownNotifications[i - 1]->ListEntry;
        entry->Flink = i + 1 == g_shutdownNotifications.size()
                           ? &g_shutdownNotificationHead
                           : &g_shutdownNotifications[i + 1]->ListEntry;
    }
    g_shutdownNotificationHead.Flink = &g_shutdownNotifications.front()->ListEntry;
    g_shutdownNotificationHead.Blink = &g_shutdownNotifications.back()->ListEntry;
}
}

extern "C" void __stdcall K_HalRegisterShutdownNotification(
    XboxHalShutdownRegistration* registration, BOOLEAN registerNotification) {
    if (!IsGuestRangeAccessible(registration, sizeof(*registration), true)) return;
    std::lock_guard<std::mutex> lock(g_shutdownNotificationMutex);
    auto found = std::find(g_shutdownNotifications.begin(), g_shutdownNotifications.end(), registration);
    if (registerNotification) {
        if (g_shutdownNotificationsInvoked.load(std::memory_order_acquire) ||
            found != g_shutdownNotifications.end() || !registration->NotificationRoutine ||
            !IsExecutableGuestAddress(reinterpret_cast<const void*>(registration->NotificationRoutine)))
            return;
        auto position = std::find_if(g_shutdownNotifications.begin(), g_shutdownNotifications.end(),
            [registration](const XboxHalShutdownRegistration* current) {
                return current->Priority < registration->Priority;
            });
        g_shutdownNotifications.insert(position, registration);
        RebuildShutdownNotificationListLocked();
        return;
    }
    if (found == g_shutdownNotifications.end()) return;
    g_shutdownNotifications.erase(found);
    registration->ListEntry.Flink = nullptr;
    registration->ListEntry.Blink = nullptr;
    RebuildShutdownNotificationListLocked();
}

void XboxKernelInvokeShutdownNotifications() {
    std::vector<XboxHalShutdownRegistration*> callbacks;
    {
        std::lock_guard<std::mutex> lock(g_shutdownNotificationMutex);
        if (g_shutdownNotificationsInvoked.exchange(true, std::memory_order_acq_rel)) return;
        callbacks.swap(g_shutdownNotifications);
        g_shutdownNotificationHead.Flink = &g_shutdownNotificationHead;
        g_shutdownNotificationHead.Blink = &g_shutdownNotificationHead;
        for (XboxHalShutdownRegistration* registration : callbacks) {
            if (IsGuestRangeAccessible(registration, sizeof(*registration), true)) {
                registration->ListEntry.Flink = nullptr;
                registration->ListEntry.Blink = nullptr;
            }
        }
    }
    // Callbacks run without the registry lock, in descending Priority order.
    for (XboxHalShutdownRegistration* registration : callbacks) {
        if (!IsGuestRangeAccessible(registration, sizeof(*registration), false)) continue;
        const auto routine = registration->NotificationRoutine;
        if (IsExecutableGuestAddress(reinterpret_cast<const void*>(routine)))
            routine(registration);
    }
}

void XboxKernelResetShutdownNotifications() {
    std::lock_guard<std::mutex> lock(g_shutdownNotificationMutex);
    for (XboxHalShutdownRegistration* registration : g_shutdownNotifications) {
        if (IsGuestRangeAccessible(registration, sizeof(*registration), true)) {
            registration->ListEntry.Flink = nullptr;
            registration->ListEntry.Blink = nullptr;
        }
    }
    g_shutdownNotifications.clear();
    g_shutdownNotificationHead.Flink = &g_shutdownNotificationHead;
    g_shutdownNotificationHead.Blink = &g_shutdownNotificationHead;
    g_shutdownNotificationsInvoked.store(false, std::memory_order_release);
}
