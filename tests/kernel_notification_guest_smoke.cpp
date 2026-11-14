#include "../guest32/kernel_system.h"
#include <cstdio>

static LONG g_order[4]{};
static ULONG g_orderCount = 0;

static void __stdcall RecordShutdown(XboxHalShutdownRegistration* registration) {
    if (g_orderCount < _countof(g_order)) g_order[g_orderCount++] = registration->Priority;
}
static void __stdcall RecordRemoved(XboxHalShutdownRegistration*) {
    if (g_orderCount < _countof(g_order)) g_order[g_orderCount++] = 999;
}
static int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main() {
    XboxKernelResetShutdownNotifications();
    XboxHalShutdownRegistration low{RecordShutdown, 0, {}};
    XboxHalShutdownRegistration high{RecordShutdown, 10, {}};
    XboxHalShutdownRegistration middle{RecordShutdown, 5, {}};
    XboxHalShutdownRegistration removed{RecordRemoved, 20, {}};

    K_HalRegisterShutdownNotification(&low, TRUE);
    K_HalRegisterShutdownNotification(&high, TRUE);
    K_HalRegisterShutdownNotification(&middle, TRUE);
    K_HalRegisterShutdownNotification(&removed, TRUE);
    if (!low.ListEntry.Flink || !low.ListEntry.Blink ||
        !high.ListEntry.Flink || !middle.ListEntry.Flink)
        return Fail("registration list links were not populated");

    // Duplicate registration is ignored; unregister removes the high-priority
    // callback before shutdown, so only the remaining three? No: removed is 20.
    K_HalRegisterShutdownNotification(&low, TRUE);
    K_HalRegisterShutdownNotification(&removed, FALSE);
    if (removed.ListEntry.Flink || removed.ListEntry.Blink)
        return Fail("unregistered entry remained linked");

    XboxKernelInvokeShutdownNotifications();
    if (g_orderCount != 3 || g_order[0] != 10 || g_order[1] != 5 || g_order[2] != 0)
        return Fail("callbacks did not run once in descending priority order");
    if (low.ListEntry.Flink || low.ListEntry.Blink ||
        high.ListEntry.Flink || high.ListEntry.Blink ||
        middle.ListEntry.Flink || middle.ListEntry.Blink)
        return Fail("list links were not cleared before callback dispatch");

    XboxKernelInvokeShutdownNotifications();
    if (g_orderCount != 3)
        return Fail("shutdown notifications ran more than once");

    std::puts("PASS: HalRegisterShutdownNotification x86 ABI, priority order, unregister, and one-shot dispatch");
    return 0;
}
