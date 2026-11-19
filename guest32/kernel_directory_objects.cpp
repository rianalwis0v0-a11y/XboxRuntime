#include "kernel_directory_objects.h"
#include "kernel_object_manager.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

#pragma pack(push, 4)
struct GuestAnsiStringDirectory { USHORT Length; USHORT MaximumLength; char* Buffer; };
struct GuestObjectAttributesDirectory {
    HANDLE RootDirectory;
    GuestAnsiStringDirectory* ObjectName;
    ULONG Attributes;
};
#pragma pack(pop)
static_assert(sizeof(GuestAnsiStringDirectory) == 8,
              "Xbox ANSI_STRING x86 layout");
static_assert(sizeof(GuestObjectAttributesDirectory) == 12,
              "Xbox OBJECT_ATTRIBUTES x86 layout");

namespace {
struct GuestDirectoryObject {
    std::string name;
    std::string lookupName;
    HANDLE handle = nullptr;
};
std::mutex g_directoryObjectMutex;
std::unordered_map<std::string, GuestDirectoryObject*> g_namedDirectories;

bool GuestSpanAccessible(const void* pointer, std::size_t length, bool write) {
    if (!length) return true;
    if (!pointer) return false;
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > UINTPTR_MAX - start) return false;
    const std::uintptr_t end = start + length;
    std::uintptr_t cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = memory.Protect & 0xFFu;
        const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (write ? !writable : !readable) return false;
        const std::uintptr_t region = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > UINTPTR_MAX - region) return false;
        const std::uintptr_t next = region + memory.RegionSize;
        if (next <= cursor) return false;
        cursor = (std::min)(next, end);
    }
    return true;
}

std::string FoldDirectoryName(const std::string& value) {
    std::string folded = value;
    for (char& ch : folded)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return folded;
}
void DestroyDirectoryObject(void* raw) {
    auto* object = static_cast<GuestDirectoryObject*>(raw);
    if (!object) return;
    {
        std::lock_guard<std::mutex> lock(g_directoryObjectMutex);
        if (!object->lookupName.empty()) {
            auto it = g_namedDirectories.find(object->lookupName);
            if (it != g_namedDirectories.end() && it->second == object)
                g_namedDirectories.erase(it);
        }
    }
    delete object;
}
}

extern "C" LONG __stdcall K_NtCreateDirectoryObject(
    HANDLE* directoryHandle, void* rawAttributes) {
    if (!directoryHandle || !rawAttributes) return LONG(0xC000000Du);
    *directoryHandle = nullptr;
    const auto* attributes =
        static_cast<const GuestObjectAttributesDirectory*>(rawAttributes);
    if (attributes->RootDirectory) return LONG(0xC00000BBu);
    GuestDirectoryObject* object = nullptr;
    try {
        std::string name;
        if (attributes->ObjectName) {
            const auto* objectName = attributes->ObjectName;
            if ((objectName->Length && !objectName->Buffer) ||
                objectName->Length > objectName->MaximumLength)
                return LONG(0xC000000Du);
            if (objectName->Length)
                name.assign(objectName->Buffer, objectName->Length);
        }
        object = new (std::nothrow) GuestDirectoryObject{};
        if (!object) return LONG(0xC0000017u);
        object->name = std::move(name);
        object->lookupName = FoldDirectoryName(object->name);
        if (!object->lookupName.empty()) {
            std::lock_guard<std::mutex> lock(g_directoryObjectMutex);
            if (g_namedDirectories.find(object->lookupName) != g_namedDirectories.end()) {
                delete object;
                return LONG(0xC0000035u); // STATUS_OBJECT_NAME_COLLISION
            }
            g_namedDirectories.emplace(object->lookupName, object);
        }
        HANDLE handle = XboxKernelCreateObjectHandle(
            object, XboxKernelObjectType::Directory, DestroyDirectoryObject);
        if (!handle) {
            DestroyDirectoryObject(object);
            return LONG(0xC0000017u);
        }
        object->handle = handle;
        *directoryHandle = handle;
        object = nullptr; // ownership transferred to the object manager
        return 0;
    } catch (...) {
        if (object) DestroyDirectoryObject(object);
        return LONG(0xC0000017u);
    }
}

extern "C" LONG __stdcall K_NtOpenDirectoryObject(
    HANDLE* directoryHandle, ULONG desiredAccess, void* rawAttributes) {
    constexpr LONG kStatusAccessViolation = static_cast<LONG>(0xC0000005u);
    constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusObjectNameNotFound = static_cast<LONG>(0xC0000034u);
    constexpr LONG kStatusInsufficientResources = static_cast<LONG>(0xC000009Au);
    if (!GuestSpanAccessible(directoryHandle, sizeof(*directoryHandle), true))
        return kStatusAccessViolation;
    HANDLE empty = nullptr;
    std::memcpy(directoryHandle, &empty, sizeof(empty));
    if (!GuestSpanAccessible(rawAttributes, sizeof(GuestObjectAttributesDirectory), false))
        return kStatusAccessViolation;
    GuestObjectAttributesDirectory attributes{};
    std::memcpy(&attributes, rawAttributes, sizeof(attributes));
    if (attributes.RootDirectory || !attributes.ObjectName)
        return kStatusInvalidParameter;
    if (!GuestSpanAccessible(attributes.ObjectName, sizeof(GuestAnsiStringDirectory), false))
        return kStatusAccessViolation;
    GuestAnsiStringDirectory objectName{};
    std::memcpy(&objectName, attributes.ObjectName, sizeof(objectName));
    if (!objectName.Length || objectName.Length > objectName.MaximumLength ||
        objectName.Length > 0x1000 ||
        !GuestSpanAccessible(objectName.Buffer, objectName.Length, false))
        return kStatusInvalidParameter;
    std::string name;
    try {
        name.assign(objectName.Buffer, objectName.Length);
    } catch (...) {
        return kStatusInsufficientResources;
    }
    const std::string lookupName = FoldDirectoryName(name);
    if (lookupName.empty()) return kStatusInvalidParameter;
    HANDLE opened = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_directoryObjectMutex);
        const auto it = g_namedDirectories.find(lookupName);
        if (it == g_namedDirectories.end() || !it->second || !it->second->handle)
            return kStatusObjectNameNotFound;
        HANDLE source = it->second->handle;
        const LONG status = XboxKernelDuplicateObjectHandle(source, &opened, 0);
        if (status != 0) return status;
    }
    std::memcpy(directoryHandle, &opened, sizeof(opened));
    (void)desiredAccess; // Access checks require Xbox OBJECT_DIRECTORY security metadata.
    return 0;
}
