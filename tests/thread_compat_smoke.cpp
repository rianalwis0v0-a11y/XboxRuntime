#include "../guest32/thread_compat.h"

#include <cstdio>
#include <cstdint>

struct ThreadProbe {
    HANDLE finished = nullptr;
    volatile LONG workerRan = 0;
    volatile LONG contextValid = 0;
};

static VOID NTAPI ProbeStart(PVOID parameter) {
    auto* probe = static_cast<ThreadProbe*>(parameter);
    InterlockedExchange(&probe->workerRan, 1);
}

static VOID NTAPI ProbeSystemRoutine(XboxGuestStartRoutine startRoutine, PVOID context) {
    auto* probe = static_cast<ThreadProbe*>(context);
    XboxGuestKThread* thread = K_KeGetCurrentThread();
    const std::uintptr_t tlsBase = reinterpret_cast<std::uintptr_t>(thread->tlsData);
    const bool tlsAligned = thread->tlsData &&
        (((tlsBase + sizeof(std::uint32_t)) & 0x0Fu) == 0);
    const bool stackValid = thread->stackBase && thread->stackLimit &&
        (reinterpret_cast<std::uintptr_t>(thread->stackBase) >
         reinterpret_cast<std::uintptr_t>(thread->stackLimit));
    InterlockedExchange(&probe->contextValid, tlsAligned && stackValid ? 1 : 0);

    if (startRoutine)
        startRoutine(context);
    SetEvent(probe->finished);
}

int main() {
    XboxKernelInitializeCurrentThread();
    XboxGuestKThread* current = K_KeGetCurrentThread();
    if (!current || !current->stackBase || !current->stackLimit ||
        reinterpret_cast<std::uintptr_t>(current->stackBase) <=
            reinterpret_cast<std::uintptr_t>(current->stackLimit)) {
        std::fprintf(stderr, "FAIL: current Xbox KTHREAD stack bounds are invalid.\n");
        return 1;
    }

    ThreadProbe probe{};
    probe.finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!probe.finished) {
        std::fprintf(stderr, "FAIL: CreateEventW failed.\n");
        return 2;
    }

    HANDLE thread = nullptr;
    HANDLE threadId = nullptr;
    const LONG status = K_PsCreateSystemThreadEx(
        &thread, 0, 0x4000, 32, &threadId, ProbeStart, &probe,
        TRUE, FALSE, ProbeSystemRoutine);
    if (status != 0 || !thread || !threadId) {
        std::fprintf(stderr,
            "FAIL: PsCreateSystemThreadEx status=0x%08X handle=%p id=%p\n",
            static_cast<unsigned>(status), thread, threadId);
        CloseHandle(probe.finished);
        return 3;
    }

    if (WaitForSingleObject(probe.finished, 0) == WAIT_OBJECT_0) {
        std::fprintf(stderr, "FAIL: CREATE_SUSPENDED thread ran before resume.\n");
        CloseHandle(thread);
        CloseHandle(probe.finished);
        return 4;
    }

    if (ResumeThread(thread) == DWORD(-1) ||
        WaitForSingleObject(probe.finished, 5000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) {
        std::fprintf(stderr, "FAIL: suspended system thread did not resume/finish.\n");
        CloseHandle(thread);
        CloseHandle(probe.finished);
        return 5;
    }

    if (probe.workerRan != 1 || probe.contextValid != 1) {
        std::fprintf(stderr,
            "FAIL: workerRan=%ld contextValid=%ld\n",
            probe.workerRan, probe.contextValid);
        CloseHandle(thread);
        CloseHandle(probe.finished);
        return 6;
    }

    CloseHandle(thread);
    CloseHandle(probe.finished);
    std::puts("PASS: PsCreateSystemThreadEx stdcall ABI, suspended start, KTHREAD stack/TLS layout.");
    return 0;
}
