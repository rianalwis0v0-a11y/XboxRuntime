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
    return {static_cast<USHORT>(length), static_cast<USHORT>(length),
            const_cast<char*>(value)};
}

int main() {
    XboxPsStatistics stats{};
    stats.Length = sizeof(stats);
    if (K_PsQueryStatistics(&stats) != 0 || stats.Length != sizeof(stats) ||
        stats.ThreadCount == 0 || stats.HandleCount == 0)
        return Fail("PsQueryStatistics host-process snapshot failed");

    auto name = ObjectString("\\??\\D:");
    auto target = ObjectString("\\Device\\Harddisk0\\Partition1");
    if (K_IoCreateSymbolicLink(&name, &target) != 0)
        return Fail("IoCreateSymbolicLink failed");
    std::wstring path = L"\\??\\d:\\games\\default.xbe";
    if (!XboxKernelResolveSymbolicLinks(&path) ||
        path != L"\\Device\\Harddisk0\\Partition1\\games\\default.xbe")
        return Fail("case-insensitive symbolic-link resolution failed");
    if (K_IoDeleteSymbolicLink(&name) != 0)
        return Fail("IoDeleteSymbolicLink failed");

    auto a = ObjectString("\\Device\\A");
    auto b = ObjectString("\\Device\\B");
    auto toB = ObjectString("\\Device\\B");
    auto toA = ObjectString("\\Device\\A");
    if (K_IoCreateSymbolicLink(&a, &toB) != 0 ||
        K_IoCreateSymbolicLink(&b, &toA) != 0)
        return Fail("cycle setup failed");
    std::wstring cyclic = L"\\Device\\A\\child";
    if (XboxKernelResolveSymbolicLinks(&cyclic))
        return Fail("symbolic-link cycle was accepted");
    K_IoDeleteSymbolicLink(&a);
    K_IoDeleteSymbolicLink(&b);

    std::puts("PASS: x64 host PsQueryStatistics and symbolic-link namespace tests");
    return 0;
}
