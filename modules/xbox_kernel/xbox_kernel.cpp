#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../xbe_loader/xbe_backend_api.h"
#include "../../guest32/guest_protocol.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct BackendContext {
    HANDLE mapping = nullptr;
    std::uint8_t* hostView = nullptr;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process{};
    std::wstring mappingName;
    std::wstring pipeName;
    std::wstring titlePath;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    bool importsResolved = false;
    bool started = false;
    int lastReserveResult = 0;
    DWORD lastReserveWin32Error = ERROR_SUCCESS;
    DWORD lastReserveChildExitCode = 0;
};
BackendContext g_context;
std::mutex g_mutex;
HINSTANCE g_module = nullptr;
std::atomic<std::uint32_t> g_sequence{1};

void Log(const wchar_t* message) {
    OutputDebugStringW(L"[XboxKernel] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}
bool ReadExact(HANDLE handle, void* output, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD received = 0;
        if (!ReadFile(handle, static_cast<std::uint8_t*>(output) + done,
                      bytes - done, &received, nullptr) || received == 0)
            return false;
        done += received;
    }
    return true;
}
bool WriteExact(HANDLE handle, const void* input, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD sent = 0;
        if (!WriteFile(handle, static_cast<const std::uint8_t*>(input) + done,
                       bytes - done, &sent, nullptr) || sent == 0)
            return false;
        done += sent;
    }
    return true;
}
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'\"') result += L"\\";
        result += c;
    }
    result += L"\"";
    return result;
}
bool ModuleDirectory(std::wstring& directory) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(g_module, path, _countof(path));
    if (!length || length >= _countof(path)) return false;
    directory.assign(path, length);
    const std::size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    directory.resize(slash);
    return true;
}
bool RuntimeEepromPath(std::wstring& path) {
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, _countof(localAppData));
    if (!length || length >= _countof(localAppData)) return false;
    const std::wstring directory =
        std::wstring(localAppData, length) + L"\\XboxRuntime";
    path = directory + L"\\eeprom.bin";
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return true;
}
void ResetContextHandles(BackendContext& context) {
    if (context.pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(context.pipe);
        context.pipe = INVALID_HANDLE_VALUE;
    }
    if (context.process.hThread) {
        CloseHandle(context.process.hThread);
        context.process.hThread = nullptr;
    }
    if (context.process.hProcess) {
        CloseHandle(context.process.hProcess);
        context.process.hProcess = nullptr;
    }
    if (context.hostView) {
        UnmapViewOfFile(context.hostView);
        context.hostView = nullptr;
    }
    if (context.mapping) {
        CloseHandle(context.mapping);
        context.mapping = nullptr;
    }
    context.mappingName.clear();
    context.pipeName.clear();
    context.titlePath.clear();
    context.base = context.size = 0;
    context.importsResolved = false;
    context.started = false;
    context.process = {};
}
void StopGuestProcessLocked(BackendContext& context) {
    if (context.process.hProcess &&
        WaitForSingleObject(context.process.hProcess, 0) == WAIT_TIMEOUT) {
        if (context.pipe != INVALID_HANDLE_VALUE) {
            XboxGuestCommandMessage message{};
            message.magic = XBOX_GUEST_PROTOCOL_MAGIC;
            message.command = XboxGuestStop;
            XboxGuestCommandReply reply{};
            if (WriteExact(context.pipe, &message, sizeof(message)))
                ReadExact(context.pipe, &reply, sizeof(reply));
        }
        if (WaitForSingleObject(context.process.hProcess, 5000) == WAIT_TIMEOUT) {
            TerminateProcess(context.process.hProcess, ERROR_OPERATION_ABORTED);
            WaitForSingleObject(context.process.hProcess, 5000);
        }
    }
    if (context.pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(context.pipe);
        context.pipe = INVALID_HANDLE_VALUE;
    }
    if (context.process.hThread) {
        CloseHandle(context.process.hThread);
        context.process.hThread = nullptr;
    }
    if (context.process.hProcess) {
        CloseHandle(context.process.hProcess);
        context.process.hProcess = nullptr;
    }
    context.process = {};
    context.importsResolved = false;
    context.started = false;
}
int __cdecl ReserveImage(std::uint32_t guestBase, std::uint32_t imageSize,
                         std::uint8_t** guestImage, void*) {
    if (!guestImage) return 1;
    *guestImage = nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    BackendContext& context = g_context;
    context.lastReserveResult = 0;
    context.lastReserveWin32Error = ERROR_SUCCESS;
    context.lastReserveChildExitCode = 0;
    if (context.mapping || context.process.hProcess || !guestBase || !imageSize ||
        (guestBase & 0xFFFFu) != 0 ||
        std::uint64_t(guestBase) + imageSize > 0x80000000ull) {
        context.lastReserveResult = 2;
        context.lastReserveWin32Error = ERROR_INVALID_PARAMETER;
        return 2;
    }

    const std::uint32_t sequence = g_sequence.fetch_add(1);
    const std::wstring suffix = std::to_wstring(GetCurrentProcessId()) +
                                L"_" + std::to_wstring(sequence);
    context.mappingName = L"Local\\XboxRuntime_XBE_" + suffix;
    context.pipeName = L"\\\\.\\pipe\\XboxRuntime_XBE_" + suffix;
    context.base = guestBase;
    context.size = imageSize;

    context.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_EXECUTE_READWRITE, 0, imageSize, context.mappingName.c_str());
    if (!context.mapping) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 3;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 3;
    }
    void* view = MapViewOfFileEx(context.mapping, FILE_MAP_ALL_ACCESS, 0, 0,
        imageSize, reinterpret_cast<void*>(static_cast<std::uintptr_t>(guestBase)));
    if (view != reinterpret_cast<void*>(static_cast<std::uintptr_t>(guestBase))) {
        const DWORD error = GetLastError();
        if (view) UnmapViewOfFile(view);
        context.lastReserveResult = 4;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 4;
    }
    context.hostView = static_cast<std::uint8_t*>(view);

    context.pipe = CreateNamedPipeW(context.pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 5000, nullptr);
    if (context.pipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 5;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 5;
    }

    std::wstring directory;
    std::wstring eeprom;
    if (!ModuleDirectory(directory) || !RuntimeEepromPath(eeprom)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 6;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 6;
    }
    const std::wstring guestExe = directory + L"\\xbe_guest32.exe";
    if (GetFileAttributesW(guestExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        Log(L"xbe_guest32.exe was not found beside xbox_kernel.dll.");
        context.lastReserveResult = 7;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 7;
    }
    std::wstring command = Quote(guestExe) +
        L" --pipe " + Quote(context.pipeName) +
        L" --mapping " + Quote(context.mappingName) +
        L" --base " + Quote(std::to_wstring(guestBase)) +
        L" --size " + Quote(std::to_wstring(imageSize)) +
        L" --eeprom " + Quote(eeprom) +
        (context.titlePath.empty() ? L"" : L" --xbe " + Quote(context.titlePath));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(guestExe.c_str(), command.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup,
        &context.process)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 8;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 8;
    }
    // Use nonblocking server mode while waiting for the child. A synchronous
    // ConnectNamedPipe can wait forever if the guest exits before opening its
    // pipe (for example, after a failed shared-view mapping).
    DWORD pipeMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(context.pipe, &pipeMode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 9;
        context.lastReserveWin32Error = error;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 9;
    }
    const ULONGLONG connectDeadline = GetTickCount64() + 10000ull;
    bool connected = false;
    for (;;) {
        if (ConnectNamedPipe(context.pipe, nullptr)) {
            connected = true;
            break;
        }
        const DWORD connectError = GetLastError();
        if (connectError == ERROR_PIPE_CONNECTED) {
            connected = true;
            break;
        }
        if (connectError != ERROR_PIPE_LISTENING &&
            connectError != ERROR_NO_DATA) {
            Log(L"Named-pipe connection failed with an unexpected error.");
            context.lastReserveResult = 9;
            context.lastReserveWin32Error = connectError;
            StopGuestProcessLocked(context);
            ResetContextHandles(context);
            return 9;
        }

        if (WaitForSingleObject(context.process.hProcess, 0) == WAIT_OBJECT_0) {
            DWORD childExitCode = ERROR_GEN_FAILURE;
            GetExitCodeProcess(context.process.hProcess, &childExitCode);
            wchar_t diagnostic[128]{};
            swprintf_s(diagnostic, _countof(diagnostic),
                       L"xbe_guest32.exe exited before pipe connect (exit=0x%08X).",
                       childExitCode);
            Log(diagnostic);
            context.lastReserveResult = 9;
            context.lastReserveChildExitCode = childExitCode;
            ResetContextHandles(context);
            return 9;
        }
        if (GetTickCount64() >= connectDeadline) {
            Log(L"Timed out waiting 10 seconds for xbe_guest32.exe to connect.");
            context.lastReserveResult = 9;
            context.lastReserveWin32Error = ERROR_TIMEOUT;
            TerminateProcess(context.process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(context.process.hProcess, 5000);
            ResetContextHandles(context);
            return 9;
        }
        Sleep(10);
    }
    pipeMode = PIPE_READMODE_BYTE | PIPE_WAIT;
    if (!connected ||
        !SetNamedPipeHandleState(context.pipe, &pipeMode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 9;
        context.lastReserveWin32Error = error;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 9;
    }

    XboxGuestHello hello{};
    if (!ReadExact(context.pipe, &hello, sizeof(hello)) ||
        hello.magic != XBOX_GUEST_PROTOCOL_MAGIC ||
        hello.version != XBOX_GUEST_PROTOCOL_VERSION ||
        hello.status != ERROR_SUCCESS ||
        hello.imageBase != guestBase || hello.imageSize != imageSize) {
        context.lastReserveResult = 10;
        context.lastReserveWin32Error = hello.status != ERROR_SUCCESS
            ? hello.status : ERROR_INVALID_DATA;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 10;
    }
    context.lastReserveResult = 0;
    *guestImage = context.hostView;
    return 0;
}
void __cdecl ReleaseImage(std::uint8_t* guestImage, void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (guestImage && guestImage != g_context.hostView) return;
    StopGuestProcessLocked(g_context);
    ResetContextHandles(g_context);
}
int __cdecl ResolveImports(std::uint8_t* guestImage,
                           const XBE_IMAGE_INFO* info, void*) {
    if (!info || !guestImage) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    BackendContext& context = g_context;
    if (guestImage != context.hostView || context.pipe == INVALID_HANDLE_VALUE ||
        info->baseAddress != context.base || info->imageSize != context.size ||
        context.importsResolved)
        return 2;
    XboxGuestCommandMessage message{
        XBOX_GUEST_PROTOCOL_MAGIC, XboxGuestResolveImports,
        info->baseAddress, info->imageSize, info->headerSize,
        info->entryPoint, info->tlsAddress, info->sectionCount,
        info->nonKernelImportDirectoryAddress, info->kernelThunkAddress
    };
    XboxGuestCommandReply reply{};
    if (!WriteExact(context.pipe, &message, sizeof(message)) ||
        !ReadExact(context.pipe, &reply, sizeof(reply)) ||
        reply.magic != XBOX_GUEST_PROTOCOL_MAGIC ||
        reply.command != XboxGuestResolveImports)
        return 3;
    if (reply.status != ERROR_SUCCESS) return static_cast<int>(reply.status);
    context.importsResolved = true;
    return 0;
}
int __cdecl StartGuest(const XBE_IMAGE_INFO* info,
                       std::uint8_t* guestImage, void*) {
    if (!info || !guestImage) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    BackendContext& context = g_context;
    if (guestImage != context.hostView || !context.importsResolved ||
        context.started || info->baseAddress != context.base ||
        info->imageSize != context.size)
        return 2;
    XboxGuestCommandMessage message{};
    message.magic = XBOX_GUEST_PROTOCOL_MAGIC;
    message.command = XboxGuestStart;
    XboxGuestCommandReply reply{};
    if (!WriteExact(context.pipe, &message, sizeof(message)) ||
        !ReadExact(context.pipe, &reply, sizeof(reply)) ||
        reply.magic != XBOX_GUEST_PROTOCOL_MAGIC ||
        reply.command != XboxGuestStart)
        return 3;
    if (reply.status != ERROR_SUCCESS)
        return static_cast<int>(reply.status);
    context.started = true;
    return 0;
}
extern "C" int __cdecl XboxKernelWaitForGuestExit(
    std::uint32_t timeoutMilliseconds, std::uint32_t* exitCode,
    std::uint32_t* tlsIndexCleared) {
    if (!exitCode || !tlsIndexCleared) return ERROR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(g_mutex);
    BackendContext& context = g_context;
    if (!context.started || context.pipe == INVALID_HANDLE_VALUE ||
        !context.process.hProcess)
        return ERROR_INVALID_STATE;

    XboxGuestCommandMessage message{};
    message.magic = XBOX_GUEST_PROTOCOL_MAGIC;
    message.command = XboxGuestWaitForExit;
    message.imageBase = timeoutMilliseconds;
    XboxGuestExitReply reply{};
    if (!WriteExact(context.pipe, &message, sizeof(message)) ||
        !ReadExact(context.pipe, &reply, sizeof(reply)) ||
        reply.magic != XBOX_GUEST_PROTOCOL_MAGIC ||
        reply.command != XboxGuestWaitForExit)
        return ERROR_BROKEN_PIPE;
    if (reply.status != ERROR_SUCCESS) return static_cast<int>(reply.status);
    *exitCode = reply.exitCode;
    *tlsIndexCleared = reply.tlsIndexCleared;
    context.started = false;
    return ERROR_SUCCESS;
}

void __cdecl StopGuest(void*) {
    std::lock_guard<std::mutex> lock(g_mutex);
    StopGuestProcessLocked(g_context);
}
}

extern "C" int __cdecl ModuleStart() {
    Log(L"Guest x86 kernel backend initialized.");
    return 0;
}
extern "C" void __cdecl ModuleStop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    StopGuestProcessLocked(g_context);
    ResetContextHandles(g_context);
    Log(L"Guest x86 kernel backend stopped.");
}
extern "C" int __cdecl XboxKernelSetGuestTitlePath(const wchar_t* path) {
    if (!path || !*path) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_context.process.hProcess || g_context.mapping) return 2;
    g_context.titlePath = path;
    return 0;
}
extern "C" int __cdecl XboxKernelGetLastReserveDiagnostic(
    int* reserveResult, DWORD* win32Error, DWORD* childExitCode) {
    if (!reserveResult || !win32Error || !childExitCode)
        return ERROR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(g_mutex);
    *reserveResult = g_context.lastReserveResult;
    *win32Error = g_context.lastReserveWin32Error;
    *childExitCode = g_context.lastReserveChildExitCode;
    return ERROR_SUCCESS;
}
extern "C" int __cdecl XBE_GetExecutionBackendV2(XBE_EXECUTION_BACKEND_V2* backend) {
    if (!backend || backend->size < sizeof(XBE_EXECUTION_BACKEND_V2)) return 1;
    XBE_EXECUTION_BACKEND_V2 value{};
    value.size = sizeof(value);
    value.version = XBE_EXECUTION_BACKEND_V2_VERSION;
    value.context = &g_context;
    value.reserveImage = ReserveImage;
    value.releaseImage = ReleaseImage;
    value.resolveImports = ResolveImports;
    value.startGuest = StartGuest;
    value.stopGuest = StopGuest;
    *backend = value;
    return 0;
}
extern "C" int __cdecl XBE_GetExecutionBackend(XBE_EXECUTION_BACKEND*) {
    // ABI v1 lacks the complete startup metadata needed by the separate x86
    // process. Keep the symbol but refuse to pretend it is safe to use.
    return 1;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_module = instance;
    return TRUE;
}
