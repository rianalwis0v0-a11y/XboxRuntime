#include "kernel_object_manager.h"
#include <mutex>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {
struct ObjectRecord {
    void* payload = nullptr;
    XboxKernelObjectType type{};
    XboxKernelObjectDestroy destroy = nullptr;
    uint64_t references = 0;
};
std::mutex g_mutex;
std::unordered_map<HANDLE, std::shared_ptr<ObjectRecord>> g_handles;
std::unordered_map<void*, std::shared_ptr<ObjectRecord>> g_objects;
uint32_t g_nextToken = 0xE0000001u;
bool g_shuttingDown = false;
thread_local XboxKernelObjectLookupStatus g_lastLookup =
    XboxKernelObjectLookupStatus::InvalidHandle;

HANDLE AllocateTokenLocked() {
    for (uint64_t attempts = 0; attempts < 0x0FFFFFFFull; ++attempts) {
        const uint32_t token = g_nextToken++;
        if (g_nextToken < 0xE0000000u) g_nextToken = 0xE0000001u;
        HANDLE value = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(token));
        if (value && g_handles.find(value) == g_handles.end()) return value;
    }
    return nullptr;
}
XboxKernelObjectDestroy DropReferenceLocked(
    const std::shared_ptr<ObjectRecord>& record, void** payloadToDestroy) {
    if (!record || record->references == 0) return nullptr;
    --record->references;
    if (record->references != 0) return nullptr;
    auto it = g_objects.find(record->payload);
    if (it != g_objects.end() && it->second == record) g_objects.erase(it);
    *payloadToDestroy = record->payload;
    return record->destroy;
}
void DestroyOutsideLock(XboxKernelObjectDestroy destroy, void* payload) {
    if (destroy && payload) destroy(payload);
}
}

HANDLE XboxKernelCreateObjectHandle(void* object, XboxKernelObjectType type,
                                    XboxKernelObjectDestroy destroy) {
    if (!object || !destroy) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_shuttingDown || g_objects.find(object) != g_objects.end()) return nullptr;
    HANDLE token = AllocateTokenLocked();
    if (!token) return nullptr;
    auto record = std::make_shared<ObjectRecord>();
    record->payload = object;
    record->type = type;
    record->destroy = destroy;
    record->references = 1;
    g_objects.emplace(object, record);
    g_handles.emplace(token, std::move(record));
    return token;
}

bool XboxKernelAcquireObjectReference(HANDLE handle, XboxKernelObjectType expectedType,
                                      void** object) {
    if (object) *object = nullptr;
    if (!object || !handle) {
        g_lastLookup = XboxKernelObjectLookupStatus::InvalidParameter;
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_shuttingDown) {
        g_lastLookup = XboxKernelObjectLookupStatus::ShuttingDown;
        return false;
    }
    auto it = g_handles.find(handle);
    if (it == g_handles.end()) {
        g_lastLookup = XboxKernelObjectLookupStatus::InvalidHandle;
        return false;
    }
    if (it->second->type != expectedType) {
        g_lastLookup = XboxKernelObjectLookupStatus::TypeMismatch;
        return false;
    }
    ++it->second->references;
    *object = it->second->payload;
    g_lastLookup = XboxKernelObjectLookupStatus::Success;
    return true;
}

void XboxKernelReleaseObjectReference(void* object) {
    if (!object) return;
    XboxKernelObjectDestroy destroy = nullptr;
    void* payload = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_objects.find(object);
        if (it == g_objects.end()) return;
        destroy = DropReferenceLocked(it->second, &payload);
    }
    DestroyOutsideLock(destroy, payload);
}

XboxKernelObjectLookupStatus XboxKernelGetLastObjectLookupStatus() {
    return g_lastLookup;
}

LONG XboxKernelDuplicateObjectHandle(HANDLE source, HANDLE* target, ULONG options) {
    if (!source || !target || (options & ~0x3u) != 0)
        return static_cast<LONG>(0xC000000Du);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_shuttingDown) return static_cast<LONG>(0xC000010Eu);
    auto it = g_handles.find(source);
    if (it == g_handles.end()) return static_cast<LONG>(0xC0000008u);
    HANDLE token = AllocateTokenLocked();
    if (!token) return static_cast<LONG>(0xC0000017u);
    ++it->second->references;
    g_handles.emplace(token, it->second);
    *target = token;
    if ((options & 0x1u) != 0) {
        auto sourceIt = g_handles.find(source);
        if (sourceIt != g_handles.end()) {
            auto record = sourceIt->second;
            g_handles.erase(sourceIt);
            // The duplicate's newly-added reference remains live.
            void* ignoredPayload = nullptr;
            (void)DropReferenceLocked(record, &ignoredPayload);
        }
    }
    return 0;
}

bool XboxKernelCloseObjectHandle(HANDLE handle) {
    if (!handle) return false;
    XboxKernelObjectDestroy destroy = nullptr;
    void* payload = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_handles.find(handle);
        if (it == g_handles.end()) return false;
        auto record = it->second;
        g_handles.erase(it);
        destroy = DropReferenceLocked(record, &payload);
    }
    DestroyOutsideLock(destroy, payload);
    return true;
}

bool XboxKernelIsObjectHandle(HANDLE handle) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_handles.find(handle) != g_handles.end();
}

void XboxKernelCloseAllObjectHandles() {
    std::vector<std::shared_ptr<ObjectRecord>> records;
    std::vector<std::pair<XboxKernelObjectDestroy, void*>> destroyList;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_shuttingDown = true;
        records.reserve(g_handles.size());
        for (auto& entry : g_handles) records.push_back(entry.second);
        g_handles.clear();
        for (const auto& record : records) {
            void* payload = nullptr;
            auto destroy = DropReferenceLocked(record, &payload);
            if (destroy && payload) destroyList.emplace_back(destroy, payload);
        }
    }
    for (const auto& item : destroyList) DestroyOutsideLock(item.first, item.second);
}

void XboxKernelBeginObjectHandleSession() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shuttingDown = false;
}
