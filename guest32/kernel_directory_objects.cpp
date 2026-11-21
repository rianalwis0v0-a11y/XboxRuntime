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
#include <vector>

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
    constexpr LONG kStatusAccessViolation = static_cast<LONG>(0xC0000005u);
    constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusInsufficientResources = static_cast<LONG>(0xC000009Au);

    if (!GuestSpanAccessible(directoryHandle, sizeof(*directoryHandle), true))
        return kStatusAccessViolation;
    const HANDLE empty = nullptr;
    std::memcpy(directoryHandle, &empty, sizeof(empty));
    if (!GuestSpanAccessible(rawAttributes, sizeof(GuestObjectAttributesDirectory), false))
        return kStatusAccessViolation;

    GuestObjectAttributesDirectory attributes{};
    std::memcpy(&attributes, rawAttributes, sizeof(attributes));
    if (attributes.RootDirectory) return LONG(0xC00000BBu);

    std::string name;
    if (attributes.ObjectName) {
        if (!GuestSpanAccessible(attributes.ObjectName,
                                 sizeof(GuestAnsiStringDirectory), false))
            return kStatusAccessViolation;
        GuestAnsiStringDirectory objectName{};
        std::memcpy(&objectName, attributes.ObjectName, sizeof(objectName));
        if (objectName.Length > objectName.MaximumLength ||
            objectName.Length > 0x1000 ||
            (objectName.Length &&
             !GuestSpanAccessible(objectName.Buffer, objectName.Length, false)))
            return kStatusInvalidParameter;
        if (objectName.Length) {
            try {
                name.assign(objectName.Buffer, objectName.Length);
            } catch (...) {
                return kStatusInsufficientResources;
            }
        }
    }

    GuestDirectoryObject* object = nullptr;
    try {
        object = new (std::nothrow) GuestDirectoryObject{};
        if (!object) return kStatusInsufficientResources;
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
            return kStatusInsufficientResources;
        }
        object->handle = handle;
        std::memcpy(directoryHandle, &handle, sizeof(handle));
        object = nullptr; // ownership transferred to the object manager
        return 0;
    } catch (...) {
        if (object) DestroyDirectoryObject(object);
        return kStatusInsufficientResources;
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
    std::string lookupName;
    try {
        lookupName = FoldDirectoryName(name);
    } catch (...) {
        return kStatusInsufficientResources;
    }
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


#pragma pack(push, 4)
struct GuestObjectDirectoryInformation {
    GuestAnsiStringDirectory Name;
    ULONG Type;
};
#pragma pack(pop)
static_assert(sizeof(GuestObjectDirectoryInformation) == 12,
              "Xbox OBJECT_DIRECTORY_INFORMATION x86 layout");

extern "C" LONG __stdcall K_NtQueryDirectoryObject(
    HANDLE directoryHandle, void* buffer, ULONG length,
    BOOLEAN restartScan, ULONG* context, ULONG* returnLength) {
    constexpr LONG kStatusAccessViolation = static_cast<LONG>(0xC0000005u);
    constexpr LONG kStatusInvalidHandle = static_cast<LONG>(0xC0000008u);
    constexpr LONG kStatusObjectTypeMismatch = static_cast<LONG>(0xC0000024u);
    constexpr LONG kStatusBufferTooSmall = static_cast<LONG>(0xC0000023u);
    constexpr LONG kStatusNoMoreEntries = static_cast<LONG>(0x8000001Au);
    constexpr LONG kStatusInvalidParameter = static_cast<LONG>(0xC000000Du);
    constexpr LONG kStatusInsufficientResources = static_cast<LONG>(0xC000009Au);

    if (!GuestSpanAccessible(context, sizeof(*context), true) ||
        !buffer || !length ||
        !GuestSpanAccessible(buffer, length, true) ||
        (returnLength &&
         !GuestSpanAccessible(returnLength, sizeof(*returnLength), true)))
        return kStatusAccessViolation;

    ULONG index = 0;
    std::memcpy(&index, context, sizeof(index));
    if (restartScan) index = 0;

    void* rawDirectory = nullptr;
    if (!XboxKernelAcquireObjectReference(directoryHandle,
            XboxKernelObjectType::Directory, &rawDirectory)) {
        const auto lookup = XboxKernelGetLastObjectLookupStatus();
        if (lookup == XboxKernelObjectLookupStatus::TypeMismatch)
            return kStatusObjectTypeMismatch;
        if (lookup == XboxKernelObjectLookupStatus::InvalidParameter)
            return kStatusInvalidParameter;
        return kStatusInvalidHandle;
    }

    std::vector<std::string> children;
    try {
        const auto* directory = static_cast<const GuestDirectoryObject*>(rawDirectory);
        const std::string prefix = directory->lookupName.empty()
            ? std::string{} : directory->lookupName + "\\";
        if (!prefix.empty()) {
            std::lock_guard<std::mutex> lock(g_directoryObjectMutex);
            for (const auto& entry : g_namedDirectories) {
                const GuestDirectoryObject* child = entry.second;
                if (!child || child == directory ||
                    entry.first.size() <= prefix.size() ||
                    entry.first.compare(0, prefix.size(), prefix) != 0)
                    continue;
                const std::string remainder = entry.first.substr(prefix.size());
                if (remainder.find_first_of("\\/") != std::string::npos)
                    continue;
                const size_t nameOffset = directory->name.size() + 1u;
                if (child->name.size() <= nameOffset) continue;
                children.emplace_back(child->name.substr(nameOffset));
            }
        }
    } catch (...) {
        XboxKernelReleaseObjectReference(rawDirectory);
        return kStatusInsufficientResources;
    }
    XboxKernelReleaseObjectReference(rawDirectory);

    std::sort(children.begin(), children.end(), [](const std::string& left,
                                                   const std::string& right) {
        return FoldDirectoryName(left) < FoldDirectoryName(right);
    });
    if (index >= children.size()) {
        if (returnLength) {
            const ULONG zero = 0;
            std::memcpy(returnLength, &zero, sizeof(zero));
        }
        if (restartScan) std::memcpy(context, &index, sizeof(index));
        return kStatusNoMoreEntries;
    }

    const std::string& name = children[index];
    if (name.empty() || name.size() >= 0xFFFFu)
        return kStatusInvalidParameter;
    const ULONG required = static_cast<ULONG>(
        sizeof(GuestObjectDirectoryInformation) + name.size() + 1u);
    if (returnLength) std::memcpy(returnLength, &required, sizeof(required));
    if (length < required) {
        if (restartScan) std::memcpy(context, &index, sizeof(index));
        return kStatusBufferTooSmall;
    }

    std::vector<std::uint8_t> output;
    try {
        output.assign(required, 0);
    } catch (...) {
        return kStatusInsufficientResources;
    }
    GuestObjectDirectoryInformation information{};
    information.Name.Length = static_cast<USHORT>(name.size());
    information.Name.MaximumLength = static_cast<USHORT>(name.size() + 1u);
    information.Name.Buffer = static_cast<char*>(buffer) +
        sizeof(GuestObjectDirectoryInformation);
    information.Type = 0; // NXDK's public directory entry type is not decoded yet.
    std::memcpy(output.data(), &information, sizeof(information));
    std::memcpy(output.data() + sizeof(information), name.data(), name.size());
    output[required - 1u] = 0;
    std::memcpy(buffer, output.data(), output.size());

    ++index;
    std::memcpy(context, &index, sizeof(index));
    return 0;
}
