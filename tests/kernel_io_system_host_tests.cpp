#include "../guest32/kernel_system.h"
#include <cstdio>
#include <cstring>
#include <string>

static LONG g_shutdownOrder[3]{};
static ULONG g_shutdownCount = 0;
static void __stdcall RecordShutdown(XboxHalShutdownRegistration* registration) {
    if (g_shutdownCount < 3) g_shutdownOrder[g_shutdownCount++] = registration->Priority;
}
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

    XboxKernelResetShutdownNotifications();
    XboxHalShutdownRegistration low{RecordShutdown, 0, {}};
    XboxHalShutdownRegistration high{RecordShutdown, 10, {}};
    XboxHalShutdownRegistration middle{RecordShutdown, 5, {}};
    K_HalRegisterShutdownNotification(&low, TRUE);
    K_HalRegisterShutdownNotification(&high, TRUE);
    K_HalRegisterShutdownNotification(&middle, TRUE);
    XboxKernelInvokeShutdownNotifications();
    if (g_shutdownCount != 3 || g_shutdownOrder[0] != 10 ||
        g_shutdownOrder[1] != 5 || g_shutdownOrder[2] != 0)
        return Fail("HAL shutdown callbacks were not invoked in priority order");

    std::puts("PASS: x64 host PsQueryStatistics symbolic-link namespace, and HAL notification tests");
    return 0;
}
