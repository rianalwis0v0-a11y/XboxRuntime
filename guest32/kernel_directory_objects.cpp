#include "kernel_directory_objects.h"
#include "kernel_object_manager.h"

#include <cctype>
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
};
std::mutex g_directoryObjectMutex;
std::unordered_map<std::string, GuestDirectoryObject*> g_namedDirectories;

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
    std::string name;
    if (attributes->ObjectName) {
        const auto* objectName = attributes->ObjectName;
        if ((objectName->Length && !objectName->Buffer) ||
            objectName->Length > objectName->MaximumLength)
            return LONG(0xC000000Du);
        if (objectName->Length)
            name.assign(objectName->Buffer, objectName->Length);
    }
    auto* object = new (std::nothrow) GuestDirectoryObject{};
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
    *directoryHandle = handle;
    return 0;
}
