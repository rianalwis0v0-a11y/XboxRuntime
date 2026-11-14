#include "../guest32/kernel_system.h"
#include <cstdio>
#include <cstring>
#include <string>

static int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static XboxObjectString ObjectString(const char* value) {
    const size_t length = std::strlen(value);
    XboxObjectString result{};
    result.Length = static_cast<USHORT>(length);
    result.MaximumLength = result.Length;
    result.Buffer = const_cast<char*>(value);
    return result;
}

int main() {
    auto link = ObjectString("\\??\\D:");
    auto target = ObjectString("\\Device\\Harddisk0\\Partition1");
    if (K_IoCreateSymbolicLink(&link, &target) != 0)
        return Fail("IoCreateSymbolicLink rejected a valid drive mapping");

    std::wstring path = L"\\??\\d:\\games\\default.xbe";
    if (!XboxKernelResolveSymbolicLinks(&path))
        return Fail("valid symbolic link could not be resolved");
    if (path != L"\\Device\\Harddisk0\\Partition1\\games\\default.xbe")
        return Fail("symbolic-link target/suffix translation is incorrect");

    auto duplicateName = ObjectString("\\??\\d:");
    if (K_IoCreateSymbolicLink(&duplicateName, &target) != static_cast<LONG>(0xC0000035u))
        return Fail("duplicate symbolic-link name was not rejected");

    std::wstring boundary = L"\\??\\DD:\\games";
    if (!XboxKernelResolveSymbolicLinks(&boundary) || boundary != L"\\??\\DD:\\games")
        return Fail("symbolic-link prefix matched without a path boundary");

    auto cycleA = ObjectString("\\Device\\CycleA");
    auto cycleB = ObjectString("\\Device\\CycleB");
    auto toB = ObjectString("\\Device\\CycleB");
    auto toA = ObjectString("\\Device\\CycleA");
    if (K_IoCreateSymbolicLink(&cycleA, &toB) != 0 ||
        K_IoCreateSymbolicLink(&cycleB, &toA) != 0)
        return Fail("could not create cycle test links");
    std::wstring cyclic = L"\\Device\\CycleA\\child";
    if (XboxKernelResolveSymbolicLinks(&cyclic))
        return Fail("symbolic-link cycle was not rejected");

    if (K_IoDeleteSymbolicLink(&cycleA) != 0 ||
        K_IoDeleteSymbolicLink(&cycleB) != 0)
        return Fail("IoDeleteSymbolicLink failed to remove registered links");
    if (K_IoDeleteSymbolicLink(&cycleA) != static_cast<LONG>(0xC0000034u))
        return Fail("deleting a missing symbolic link did not return name-not-found");

    if (K_IoDeleteSymbolicLink(&link) != 0)
        return Fail("IoDeleteSymbolicLink failed for the drive mapping");
    std::wstring afterDelete = L"\\??\\D:\\games";
    if (!XboxKernelResolveSymbolicLinks(&afterDelete) || afterDelete != L"\\??\\D:\\games")
        return Fail("deleted symbolic link remained active");

    XboxObjectString invalid{};
    invalid.Length = 1;
    invalid.MaximumLength = 0;
    if (K_IoCreateSymbolicLink(&invalid, &target) != static_cast<LONG>(0xC000000Du))
        return Fail("invalid OBJECT_STRING was not rejected");

    std::puts("PASS: IoCreateSymbolicLink/IoDeleteSymbolicLink x86 ABI, prefix resolution, collisions, cycles, and errors");
    return 0;
}
