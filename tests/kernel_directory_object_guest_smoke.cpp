#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "kernel_directory_objects.h"
#include "kernel_object_manager.h"

#pragma pack(push, 4)
struct GuestAnsiString { USHORT Length, MaximumLength; char* Buffer; };
struct GuestObjectAttributes { HANDLE RootDirectory; GuestAnsiString* ObjectName; ULONG Attributes; };
#pragma pack(pop)

int main() {
    static_assert(sizeof(void*) == 4, "Xbox directory smoke test requires x86");
    char nameText[] = "\\BaseNamedObjects\\XboxRuntimeTestDirectory";
    GuestAnsiString name{};
    name.Length = static_cast<USHORT>(std::strlen(nameText));
    name.MaximumLength = name.Length;
    name.Buffer = nameText;
    GuestObjectAttributes attributes{};
    attributes.ObjectName = &name;

    HANDLE directory = nullptr;
    LONG status = K_NtCreateDirectoryObject(&directory, &attributes);
    if (status != 0 || !directory) return 1;
    void* payload = nullptr;
    if (!XboxKernelAcquireObjectReference(directory,
            XboxKernelObjectType::Directory, &payload) || !payload)
        return 2;
    XboxKernelReleaseObjectReference(payload);

    HANDLE duplicate = nullptr;
    if (XboxKernelDuplicateObjectHandle(directory, &duplicate, 0x2) != 0 ||
        !duplicate) return 3;

    HANDLE collision = nullptr;
    status = K_NtCreateDirectoryObject(&collision, &attributes);
    if (status != static_cast<LONG>(0xC0000035u) || collision != nullptr)
        return 4;

    GuestObjectAttributes relative = attributes;
    relative.RootDirectory = directory;
    if (K_NtCreateDirectoryObject(&collision, &relative) !=
        static_cast<LONG>(0xC00000BBu))
        return 5;

    if (!XboxKernelCloseObjectHandle(directory) ||
        !XboxKernelCloseObjectHandle(duplicate))
        return 6;

    // Once the last handle closes, the namespace entry is destroyed and the
    // same name can be created again.
    HANDLE recreated = nullptr;
    if (K_NtCreateDirectoryObject(&recreated, &attributes) != 0 || !recreated)
        return 7;
    if (!XboxKernelCloseObjectHandle(recreated)) return 8;

    std::puts("kernel_directory_object_guest_smoke: PASS (create, typed handle, duplicate, collision, destroy)");
    return 0;
}
