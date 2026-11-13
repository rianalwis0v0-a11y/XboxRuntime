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
    wchar_t windows[32768]{};
    const UINT length = GetWindowsDirectoryW(windows, _countof(windows));
    if (!length || length >= _countof(windows)) return false;
    std::wstring root(windows, length);
    const std::size_t slash = root.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    root.resize(slash);
    path = root + L"\\XboxRuntime\\eeprom.bin";
    const std::wstring directory = root + L"\\XboxRuntime";
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
    if (context.mapping || context.process.hProcess || !guestBase || !imageSize ||
        (guestBase & 0xFFFFu) != 0 ||
        std::uint64_t(guestBase) + imageSize > 0x80000000ull)
        return 2;

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
        ResetContextHandles(context);
        return 3;
    }
    void* view = MapViewOfFileEx(context.mapping, FILE_MAP_ALL_ACCESS, 0, 0,
        imageSize, reinterpret_cast<void*>(static_cast<std::uintptr_t>(guestBase)));
    if (view != reinterpret_cast<void*>(static_cast<std::uintptr_t>(guestBase))) {
        if (view) UnmapViewOfFile(view);
        ResetContextHandles(context);
        return 4;
    }
    context.hostView = static_cast<std::uint8_t*>(view);

    context.pipe = CreateNamedPipeW(context.pipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 5000, nullptr);
    if (context.pipe == INVALID_HANDLE_VALUE) {
        ResetContextHandles(context);
        return 5;
    }

    std::wstring directory;
    std::wstring eeprom;
    if (!ModuleDirectory(directory) || !RuntimeEepromPath(eeprom)) {
        ResetContextHandles(context);
        return 6;
    }
    const std::wstring guestExe = directory + L"\\xbe_guest32.exe";
    if (GetFileAttributesW(guestExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"xbe_guest32.exe was not found beside xbox_kernel.dll.");
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
        ResetContextHandles(context);
        return 8;
    }
    BOOL connected = ConnectNamedPipe(context.pipe, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
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
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 10;
    }
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
        reply.command != XboxGuestResolveImports || reply.status != ERROR_SUCCESS)
        return 3;
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
        reply.command != XboxGuestStart || reply.status != ERROR_SUCCESS)
        return 3;
    context.started = true;
    return 0;
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
