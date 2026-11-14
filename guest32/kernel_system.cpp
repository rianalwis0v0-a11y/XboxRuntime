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
    std::wstring converted(static_cast<size_t>(characters), L'\\0');
    if (MultiByteToWideChar(CP_ACP, 0, source->Buffer, source->Length,
                            converted.data(), characters) != characters) return false;
    if (converted.find(L'\\0') != std::wstring::npos ||
        (converted.front() != L'\\' && converted.front() != L'/')) return false;
    *value = std::move(converted);
    return true;
}
}

extern "C" LONG __stdcall K_IoCreateSymbolicLink(
    XboxObjectString* symbolicLinkName, XboxObjectString* deviceName) {
    constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusObjectNameCollision = static_cast<LONG>(0xC0000035u);
    std::wstring name, target;
    if (!ReadObjectString(symbolicLinkName, &name) ||
        !ReadObjectString(deviceName, &target)) return kStatusInvalidParameter;
    const std::wstring key = NormalizeObjectName(name);
    std::lock_guard<std::mutex> lock(g_symbolicLinkMutex);
    if (g_symbolicLinks.find(key) != g_symbolicLinks.end())
        return kStatusObjectNameCollision;
    g_symbolicLinks.emplace(key, std::move(target));
    return 0;
}

extern "C" LONG __stdcall K_IoDeleteSymbolicLink(XboxObjectString* symbolicLinkName) {
    constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
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
