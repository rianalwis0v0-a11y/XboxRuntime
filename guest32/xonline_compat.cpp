#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <algorithm>
#include <mutex>
#include <new>
#include <unordered_set>
#include "xnet_compat.h"
#include "xonline_compat.h"

struct XboxOnlineTask {
    HANDLE workEvent;
    HRESULT result;
    bool completed;
};

namespace {
std::mutex g_mutex;
bool g_started = false;
std::unordered_set<XONLINETASK_HANDLE> g_tasks;\nXONLINE_USER g_logonUsers[XONLINE_MAX_LOGON_USERS]{};

bool IsOwnedTask(XONLINETASK_HANDLE task) {
    return task != nullptr && g_tasks.find(task) != g_tasks.end();
}
}

extern "C" HRESULT WINAPI XOnlineStartup(const XONLINE_STARTUP_PARAMS* params) {
    (void)params; // The current compatibility backend does not allocate a private pool.
    if (!XboxNetIsStarted()) return XONLINE_E_NOT_INITIALIZED;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_started) return S_OK;
    g_started = true;
    return S_OK;
}

extern "C" HRESULT WINAPI XOnlineCleanup() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto* task : g_tasks) delete task;
    g_tasks.clear();
    g_started = false;
    return S_OK;
}

extern "C" HRESULT WINAPI XOnlineGetUsers(XONLINE_USER* users, DWORD* userCount) {
    if (!userCount) return E_POINTER;
    const DWORD capacity = *userCount;
    if (capacity != 0 && !users) return E_POINTER;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_started) return XONLINE_E_NOT_INITIALIZED;
    }

    // Local HDD/memory-unit account storage is not implemented yet. Return an
    // empty account list rather than fabricating a console identity.
    const DWORD clearCount = (std::min)(capacity, XONLINE_MAX_STORED_ONLINE_USERS);
    if (clearCount) ZeroMemory(users, sizeof(XONLINE_USER) * clearCount);
    *userCount = 0;
    return S_OK;
}

extern "C" HRESULT WINAPI XOnlineLogon(const XONLINE_USER* users,
    const DWORD* serviceIds, DWORD serviceCount, HANDLE workEvent,
    XONLINETASK_HANDLE* outputTask) {
    if (!outputTask) return E_POINTER;
    *outputTask = nullptr;
    if (!users || !serviceIds || serviceCount == 0)
        return E_INVALIDARG;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_started) return XONLINE_E_NOT_INITIALIZED;

    auto* task = new (std::nothrow) XboxOnlineTask{};
    if (!task) return XONLINE_E_OUT_OF_MEMORY;
    task->workEvent = workEvent;
    task->result = XONLINETASK_S_RUNNING;
    task->completed = false;
    try {
        g_tasks.insert(task);
    } catch (...) {
        delete task;
        return XONLINE_E_OUT_OF_MEMORY;
    }
    *outputTask = task;
    return S_OK;
}

extern "C" HRESULT WINAPI XOnlineTaskContinue(XONLINETASK_HANDLE task) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!IsOwnedTask(task)) return E_INVALIDARG;
    if (!g_started) return XONLINE_E_NOT_INITIALIZED;
    if (!task->completed) {
        // No real XboxNet authentication endpoint or ticket verifier is wired
        // into the guest runtime. Complete deterministically and fail closed.
        task->result = XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE;
        task->completed = true;
        if (task->workEvent) SetEvent(task->workEvent);
    }
    return task->result;
}

extern "C" HRESULT WINAPI XOnlineLogonTaskGetResults(XONLINETASK_HANDLE task) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!IsOwnedTask(task)) return E_INVALIDARG;
    return task->completed ? task->result : XONLINETASK_S_RUNNING;
}

extern "C" HRESULT WINAPI XOnlineTaskClose(XONLINETASK_HANDLE task) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!IsOwnedTask(task)) return E_INVALIDARG;
    g_tasks.erase(task);
    delete task;
    return S_OK;
}
\nextern "C" XONLINE_USER* WINAPI XOnlineGetLogonUsers() {\n    std::lock_guard<std::mutex> lock(g_mutex);\n    return g_started ? g_logonUsers : nullptr;\n}\n