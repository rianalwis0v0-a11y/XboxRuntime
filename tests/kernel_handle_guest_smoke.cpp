#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "kernel_memory.h"

static int Fail(const char* what, LONG status) {
    std::fprintf(stderr, "kernel_handle_guest_smoke: %s status=0x%08lX\n",
                 what, static_cast<unsigned long>(status));
    return 1;
}
int main() {
    static_assert(sizeof(void*) == 4, "Xbox guest ABI test must be x86");
    HANDLE source = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!source) return Fail("create source event", static_cast<LONG>(GetLastError()));

    HANDLE duplicate = nullptr;
    LONG status = K_NtDuplicateObject(source, &duplicate, 1u); // NXDK DUPLICATE_CLOSE_SOURCE
    if (status != 0 || !duplicate) return Fail("duplicate/close-source", status);

    SetLastError(ERROR_SUCCESS);
    if (WaitForSingleObject(source, 0) != WAIT_FAILED ||
        GetLastError() != ERROR_INVALID_HANDLE)
        return Fail("source handle must be closed", static_cast<LONG>(GetLastError()));

    if (!SetEvent(duplicate) || WaitForSingleObject(duplicate, 0) != WAIT_OBJECT_0)
        return Fail("duplicate must retain source access", static_cast<LONG>(GetLastError()));

    if (!CloseHandle(duplicate))
        return Fail("close duplicate", static_cast<LONG>(GetLastError()));

    HANDLE invalidTarget = nullptr;
    status = K_NtDuplicateObject(source, &invalidTarget, 2u);
    if (status != static_cast<LONG>(0xC000000Du))
        return Fail("reject unsupported duplicate options", status);

    std::puts("kernel_handle_guest_smoke: PASS (x86 stdcall, same-process duplicate, same-access, DUPLICATE_CLOSE_SOURCE)");
    return 0;
}
