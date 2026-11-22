#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../xbe_loader/xbe_backend_api.h"
#include "../../guest32/guest_protocol.h"
#include "../../guest32/kernel_gpu_service_protocol.h"
#include "../../guest32/kernel_physical_memory.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct BackendContext {
    HANDLE mapping = nullptr;
    std::uint8_t* hostView = nullptr;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE gpuPipe = INVALID_HANDLE_VALUE;
    HANDLE gpuThread = nullptr;
    HANDLE gpuMemoryMapping = nullptr;
    std::uint8_t* hostGpuMemoryView = nullptr;
    HANDLE physicalMemoryMapping = nullptr;
    std::uint8_t* hostPhysicalMemoryView = nullptr;
    std::wstring physicalMemoryMappingName;
    PROCESS_INFORMATION process{};
    std::wstring mappingName;
    std::wstring pipeName;
    std::wstring gpuPipeName;
    std::wstring gpuMemoryMappingName;
    std::wstring titlePath;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    bool importsResolved = false;
    bool started = false;
    int lastReserveResult = 0;
    DWORD lastReserveWin32Error = ERROR_SUCCESS;
    DWORD lastReserveChildExitCode = 0;
    std::uint32_t gpuStats[6]{};
    std::uint32_t gpuSurface[10]{};
    bool gpuDiagnosticsValid = false;
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
using GpuReadRegisterFn = uint64_t (__cdecl*)(uint32_t, unsigned int);
using GpuWriteRegisterFn = int (__cdecl*)(uint32_t, uint64_t, unsigned int);
using GpuSubmitMethodFn = int (__cdecl*)(uint32_t, uint32_t, uint32_t);
using GpuFlushFn = void (__cdecl*)();
using GpuMemoryReadFn = int (__cdecl*)(uint32_t, void*, uint32_t);
using GpuMemoryWriteFn = int (__cdecl*)(uint32_t, const void*, uint32_t);
using GpuGetStatsFn = int (__cdecl*)(void*);
using GpuGetSurfaceInfoFn = int (__cdecl*)(void*);

bool GpuRangeFits(uint32_t offset, uint32_t length) {
    return uint64_t(offset) + uint64_t(length) <= 0x100000000ull;
}

LRESULT CALLBACK GpuRendererWindowProc(HWND window, UINT message,
                                                WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) {
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateGpuRendererWindow() {
    const wchar_t* className = L"XboxRuntimeNv2aRendererWindow";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = GpuRendererWindowProc;
    windowClass.hInstance = g_module;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;

    HWND window = CreateWindowExW(0, className, L"XboxRuntime - NV2A",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
    return window;
}

DWORD WINAPI GpuServiceWorker(void* parameter) {
    BackendContext* context = static_cast<BackendContext*>(parameter);
    if (!context) return 1;
    HANDLE pipe = context->gpuPipe;
    std::wstring moduleDirectory;
    std::wstring providerPath;
    HMODULE provider = nullptr;
    bool providerStarted = false;
    HWND rendererWindow = nullptr;
    bool rendererReady = false;
    if (ModuleDirectory(moduleDirectory)) {
        providerPath = moduleDirectory + L"\\nv2a.dll";
        provider = LoadLibraryExW(providerPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    using ProviderStartWithMemoryFn = int (__cdecl*)(void*, uint32_t);
    const auto providerStart = reinterpret_cast<ProviderStartWithMemoryFn>(
        provider ? GetProcAddress(provider, "ModuleStartWithMemory") : nullptr);
    const auto providerStop = reinterpret_cast<void (__cdecl*)()>(
        provider ? GetProcAddress(provider, "ModuleStop") : nullptr);
    using ProviderBindPhysicalMemoryFn = int (__cdecl*)(uint32_t, void*, uint32_t);
    const auto bindPhysicalMemory = reinterpret_cast<ProviderBindPhysicalMemoryFn>(
        provider ? GetProcAddress(provider, "XboxNV2A_BindGuestPhysicalMemory") : nullptr);
    using ProviderInitializeRendererFn = int (__cdecl*)(HWND, uint32_t, uint32_t);
    const auto initializeRenderer = reinterpret_cast<ProviderInitializeRendererFn>(
        provider ? GetProcAddress(provider, "XboxNV2A_InitializeRenderer") : nullptr);
    if (provider && providerStart && context->hostGpuMemoryView &&
        providerStart(context->hostGpuMemoryView, XBOX_GPU_SHARED_MEMORY_SIZE) == 0) {
        providerStarted = true;
        if (!bindPhysicalMemory || !context->hostPhysicalMemoryView ||
            bindPhysicalMemory(XBOX_GUEST_PHYSICAL_RAM_BASE,
                context->hostPhysicalMemoryView, XBOX_GUEST_PHYSICAL_RAM_SIZE) != 0) {
            Log(L"NV2A provider could not bind the kernel-owned guest physical RAM section.");
            if (providerStop) providerStop();
            providerStarted = false;
        } else {
            Log(L"NV2A provider bound the kernel-owned 64 MiB guest physical RAM section for PCI/AGP DMA.");
        }
    } else if (provider) {
        Log(L"NV2A provider could not attach to the shared GPU memory section.");
    } else {
        Log(L"nv2a.dll not found beside xbox_kernel.dll.");
    }
    if (providerStarted && initializeRenderer) {
        rendererWindow = CreateGpuRendererWindow();
        if (!rendererWindow ||
            initializeRenderer(rendererWindow, 640u, 480u) != 0) {
            Log(L"NV2A D3D8/D3D11 renderer initialization failed; GPU register and memory services remain available.");
            if (rendererWindow) {
                DestroyWindow(rendererWindow);
                rendererWindow = nullptr;
            }
        } else {
            rendererReady = true;
            Log(L"NV2A D3D8/D3D11 renderer initialized at 640x480.");
        }
    } else if (providerStarted) {
        Log(L"NV2A provider does not export renderer initialization.");
    }
    const auto readRegister = reinterpret_cast<GpuReadRegisterFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_ReadRegister") : nullptr);
    const auto writeRegister = reinterpret_cast<GpuWriteRegisterFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_WriteRegister") : nullptr);
    const auto submitMethod = reinterpret_cast<GpuSubmitMethodFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_SubmitMethod") : nullptr);
    const auto flush = reinterpret_cast<GpuFlushFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_Flush") : nullptr);
    const auto readVram = reinterpret_cast<GpuMemoryReadFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_ReadVRAM") : nullptr);
    const auto writeVram = reinterpret_cast<GpuMemoryWriteFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_WriteVRAM") : nullptr);
    const auto readRamin = reinterpret_cast<GpuMemoryReadFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_ReadRAMIN") : nullptr);
    const auto writeRamin = reinterpret_cast<GpuMemoryWriteFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_WriteRAMIN") : nullptr);
    const auto getStats = reinterpret_cast<GpuGetStatsFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_GetStats") : nullptr);
    const auto getSurfaceInfo = reinterpret_cast<GpuGetSurfaceInfoFn>(
        providerStarted ? GetProcAddress(provider, "XboxNV2A_GetSurfaceInfo") : nullptr);
    const bool providerReady = providerStarted && rendererReady && readRegister && writeRegister &&
        submitMethod && flush && readVram && writeVram && readRamin && writeRamin;
    if (providerStarted && !providerReady) {
        Log(L"NV2A provider is missing one or more required C ABI exports.");
        if (providerStop) providerStop();
        providerStarted = false;
    }

    for (;;) {
        XboxGpuRpcRequest request{};
        if (!ReadExact(pipe, &request, sizeof(request))) break;
        if (request.magic != XBOX_GPU_RPC_MAGIC ||
            request.version != XBOX_GPU_RPC_VERSION)
            break;

        XboxGpuRpcResponse response{
            XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION,
            request.request_id, ERROR_NOT_SUPPORTED, 0
        };
        std::vector<uint8_t> transfer;
        bool sendReadPayload = false;

        switch (request.opcode) {
        case XboxGpuRpcPing:
            if (request.payload_size != 0) goto cleanup;
            response.status = providerReady ? ERROR_SUCCESS : ERROR_MOD_NOT_FOUND;
            response.value = providerReady ? XBOX_GPU_RPC_VERSION : 0;
            break;
        case XboxGpuRpcReadRegister:
            if (request.payload_size != 0) goto cleanup;
            if ((request.arg1 != 1 && request.arg1 != 2 && request.arg1 != 4) ||
                !GpuRangeFits(request.arg0, request.arg1)) {
                response.status = ERROR_INVALID_PARAMETER;
            } else if (readRegister) {
                response.value = readRegister(request.arg0, request.arg1);
                response.status = ERROR_SUCCESS;
            }
            break;
        case XboxGpuRpcWriteRegister: {
            if (request.payload_size != 0) goto cleanup;
            if ((request.arg3 != 1 && request.arg3 != 2 && request.arg3 != 4) ||
                !GpuRangeFits(request.arg0, request.arg3)) {
                response.status = ERROR_INVALID_PARAMETER;
                break;
            }
            if (writeRegister) {
                const uint64_t value = (uint64_t(request.arg2) << 32) | request.arg1;
                response.status = writeRegister(request.arg0, value, request.arg3) == 0
                    ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
            }
            break;
        }
        case XboxGpuRpcSubmitMethod:
            if (request.payload_size != 0) goto cleanup;
            if (submitMethod)
                response.status = submitMethod(request.arg0, request.arg1, request.arg2) == 0
                    ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
            break;
        case XboxGpuRpcFlush:
            if (request.payload_size != 0) goto cleanup;
            if (flush) {
                flush();
                response.status = ERROR_SUCCESS;
            }
            break;
        case XboxGpuRpcReadVram:
        case XboxGpuRpcReadRamin: {
            if (request.payload_size != 0) goto cleanup;
            if (request.arg1 > XBOX_GPU_RPC_MAX_TRANSFER ||
                !GpuRangeFits(request.arg0, request.arg1)) {
                response.status = ERROR_INVALID_PARAMETER;
                break;
            }
            try { transfer.resize(request.arg1); }
            catch (...) { response.status = ERROR_NOT_ENOUGH_MEMORY; break; }
            const auto readMemory = request.opcode == XboxGpuRpcReadVram
                ? readVram : readRamin;
            if (readMemory && (request.arg1 == 0 ||
                               readMemory(request.arg0, transfer.data(), request.arg1) == 0)) {
                response.status = ERROR_SUCCESS;
                response.value = request.arg1;
                sendReadPayload = request.arg1 != 0;
            } else if (!readMemory) {
                response.status = ERROR_NOT_SUPPORTED;
            } else {
                response.status = ERROR_GEN_FAILURE;
            }
            break;
        }
        case XboxGpuRpcWriteVram:
        case XboxGpuRpcWriteRamin: {
            if (request.payload_size > XBOX_GPU_RPC_MAX_TRANSFER ||
                request.payload_size != request.arg1 ||
                !GpuRangeFits(request.arg0, request.arg1))
                goto cleanup;
            try { transfer.resize(request.payload_size); }
            catch (...) { goto cleanup; }
            if (request.payload_size &&
                !ReadExact(pipe, transfer.data(), request.payload_size))
                goto cleanup;
            const auto writeMemory = request.opcode == XboxGpuRpcWriteVram
                ? writeVram : writeRamin;
            if (writeMemory && (request.arg1 == 0 ||
                writeMemory(request.arg0, transfer.data(), request.arg1) == 0)) {
                response.status = ERROR_SUCCESS;
            } else if (!writeMemory) {
                response.status = ERROR_NOT_SUPPORTED;
            } else {
                response.status = ERROR_GEN_FAILURE;
            }
            break;
        }
        default:
            if (request.payload_size != 0) goto cleanup;
            response.status = ERROR_NOT_SUPPORTED;
            break;
        }

        if (!WriteExact(pipe, &response, sizeof(response))) break;
        if (sendReadPayload && !WriteExact(pipe, transfer.data(), request.arg1))
            break;
    }
cleanup:
    if (providerStarted && flush)
        flush();
    if (providerStarted && getStats && getSurfaceInfo) {
        std::uint32_t stats[6]{};
        std::uint32_t surface[10]{};
        if (getStats(stats) == 0 && getSurfaceInfo(surface) == 0) {
            std::memcpy(context->gpuStats, stats, sizeof(stats));
            std::memcpy(context->gpuSurface, surface, sizeof(surface));
            context->gpuDiagnosticsValid = true;
        }
    }
    if (providerStarted && bindPhysicalMemory)
        (void)bindPhysicalMemory(0u, nullptr, 0u);
    if (providerStarted && providerStop) providerStop();
    if (rendererWindow) DestroyWindow(rendererWindow);
    if (provider) FreeLibrary(provider);
    return 0;
}

void StopGpuService(BackendContext& context) {
    if (context.gpuThread) {
        CancelSynchronousIo(context.gpuThread);
        WaitForSingleObject(context.gpuThread, INFINITE);
        CloseHandle(context.gpuThread);
        context.gpuThread = nullptr;
    }
    if (context.gpuPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(context.gpuPipe);
        CloseHandle(context.gpuPipe);
        context.gpuPipe = INVALID_HANDLE_VALUE;
    }
}

void ResetContextHandles(BackendContext& context) {
    StopGpuService(context);
    context.gpuDiagnosticsValid = false;
    std::memset(context.gpuStats, 0, sizeof(context.gpuStats));
    std::memset(context.gpuSurface, 0, sizeof(context.gpuSurface));
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
    if (context.hostGpuMemoryView) {
        UnmapViewOfFile(context.hostGpuMemoryView);
        context.hostGpuMemoryView = nullptr;
    }
    if (context.hostPhysicalMemoryView) {
        UnmapViewOfFile(context.hostPhysicalMemoryView);
        context.hostPhysicalMemoryView = nullptr;
    }
    if (context.physicalMemoryMapping) {
        CloseHandle(context.physicalMemoryMapping);
        context.physicalMemoryMapping = nullptr;
    }
    if (context.gpuMemoryMapping) {
        CloseHandle(context.gpuMemoryMapping);
        context.gpuMemoryMapping = nullptr;
    }
    if (context.mapping) {
        CloseHandle(context.mapping);
        context.mapping = nullptr;
    }
    context.mappingName.clear();
    context.pipeName.clear();
    context.gpuPipeName.clear();
    context.gpuMemoryMappingName.clear();
    context.physicalMemoryMappingName.clear();
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
    StopGpuService(context);
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
    context.gpuPipeName = L"\\\\.\\pipe\\XboxRuntime_GPU_" + suffix;
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

    context.gpuPipe = CreateNamedPipeW(context.gpuPipeName.c_str(),
        PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, XBOX_GPU_RPC_MAX_TRANSFER + sizeof(XboxGpuRpcRequest),
        XBOX_GPU_RPC_MAX_TRANSFER + sizeof(XboxGpuRpcResponse), 5000, nullptr);
    if (context.gpuPipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 5;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 5;
    }

    context.gpuMemoryMappingName = L"Local\\XboxRuntime_GPU_MEM_" + suffix;
    context.gpuMemoryMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, XBOX_GPU_SHARED_MEMORY_SIZE,
        context.gpuMemoryMappingName.c_str());
    if (!context.gpuMemoryMapping) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 5;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 5;
    }
    context.hostGpuMemoryView = static_cast<std::uint8_t*>(MapViewOfFile(
        context.gpuMemoryMapping, FILE_MAP_ALL_ACCESS, 0, 0,
        XBOX_GPU_SHARED_MEMORY_SIZE));
    if (!context.hostGpuMemoryView) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 5;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 5;
    }

    // Dedicated 64 MiB guest physical RAM section; never alias the XBE image.
    context.physicalMemoryMappingName = L"Local\\XboxRuntime_GUEST_RAM_" + suffix;
    context.physicalMemoryMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, XBOX_GUEST_PHYSICAL_RAM_SIZE,
        context.physicalMemoryMappingName.c_str());
    if (!context.physicalMemoryMapping) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 5;
        context.lastReserveWin32Error = error;
        ResetContextHandles(context);
        return 5;
    }
    context.hostPhysicalMemoryView = static_cast<std::uint8_t*>(MapViewOfFile(
        context.physicalMemoryMapping, FILE_MAP_ALL_ACCESS, 0, 0,
        XBOX_GUEST_PHYSICAL_RAM_SIZE));
    if (!context.hostPhysicalMemoryView) {
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
        L" --gpu-pipe " + Quote(context.gpuPipeName) +
        L" --gpu-memory " + Quote(context.gpuMemoryMappingName) +
        L" --physical-memory " + Quote(context.physicalMemoryMappingName) +
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

    // The GPU RPC channel is independent of the lifecycle/control pipe.
    DWORD gpuMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(context.gpuPipe, &gpuMode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 11;
        context.lastReserveWin32Error = error;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 11;
    }
    const ULONGLONG gpuConnectDeadline = GetTickCount64() + 10000ull;
    bool gpuConnected = false;
    for (;;) {
        if (ConnectNamedPipe(context.gpuPipe, nullptr)) {
            gpuConnected = true;
            break;
        }
        const DWORD connectError = GetLastError();
        if (connectError == ERROR_PIPE_CONNECTED) {
            gpuConnected = true;
            break;
        }
        if (connectError != ERROR_PIPE_LISTENING && connectError != ERROR_NO_DATA) {
            context.lastReserveResult = 11;
            context.lastReserveWin32Error = connectError;
            StopGuestProcessLocked(context);
            ResetContextHandles(context);
            return 11;
        }
        if (WaitForSingleObject(context.process.hProcess, 0) == WAIT_OBJECT_0 ||
            GetTickCount64() >= gpuConnectDeadline) {
            context.lastReserveResult = 11;
            context.lastReserveWin32Error = ERROR_TIMEOUT;
            StopGuestProcessLocked(context);
            ResetContextHandles(context);
            return 11;
        }
        Sleep(10);
    }
    gpuMode = PIPE_READMODE_BYTE | PIPE_WAIT;
    if (!gpuConnected ||
        !SetNamedPipeHandleState(context.gpuPipe, &gpuMode, nullptr, nullptr)) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 11;
        context.lastReserveWin32Error = error;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 11;
    }
    context.gpuThread = CreateThread(nullptr, 0, GpuServiceWorker,
        &context, 0, nullptr);
    if (!context.gpuThread) {
        const DWORD error = GetLastError();
        context.lastReserveResult = 11;
        context.lastReserveWin32Error = error;
        StopGuestProcessLocked(context);
        ResetContextHandles(context);
        return 11;
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
    std::uint32_t* tlsIndexCleared, std::uint32_t* exceptionInstruction,
    std::uint32_t* accessType, std::uint32_t* faultAddress,
    std::uint32_t* faultProtection, std::uint32_t* faultState,
    std::uint32_t* eax, std::uint32_t* ebx, std::uint32_t* ecx,
    std::uint32_t* edx, std::uint32_t* esi, std::uint32_t* edi,
    std::uint32_t* ebp, std::uint32_t* esp) {
    if (!exitCode || !tlsIndexCleared || !exceptionInstruction ||
        !accessType || !faultAddress || !faultProtection || !faultState ||
        !eax || !ebx || !ecx || !edx || !esi || !edi || !ebp || !esp)
        return ERROR_INVALID_PARAMETER;
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
    *exceptionInstruction = reply.exceptionInstruction;
    *accessType = reply.accessType;
    *faultAddress = reply.faultAddress;
    *faultProtection = reply.faultProtection;
    *faultState = reply.faultState;
    *eax = reply.eax; *ebx = reply.ebx; *ecx = reply.ecx; *edx = reply.edx;
    *esi = reply.esi; *edi = reply.edi; *ebp = reply.ebp; *esp = reply.esp;
    context.started = false;
    // The guest closes its GPU pipe as it exits. Let the worker flush and
    // snapshot the provider before the integration harness asks for pixels.
    if (context.process.hProcess)
        (void)WaitForSingleObject(context.process.hProcess, 10000);
    if (context.gpuThread)
        (void)WaitForSingleObject(context.gpuThread, 10000);
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
extern "C" int __cdecl XboxKernelGetGpuDiagnostics(
    std::uint32_t* stats, std::uint32_t* surface) {
    if (!stats || !surface) return ERROR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_context.gpuDiagnosticsValid) return ERROR_NOT_READY;
    std::memcpy(stats, g_context.gpuStats, sizeof(g_context.gpuStats));
    std::memcpy(surface, g_context.gpuSurface, sizeof(g_context.gpuSurface));
    return ERROR_SUCCESS;
}
extern "C" int __cdecl XboxKernelReadGpuVram(
    std::uint32_t offset, void* destination, std::uint32_t length) {
    if (!destination || !length ||
        std::uint64_t(offset) + length > XBOX_GPU_SHARED_VRAM_SIZE)
        return ERROR_INVALID_PARAMETER;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_context.hostGpuMemoryView) return ERROR_INVALID_STATE;
    std::memcpy(destination, g_context.hostGpuMemoryView + offset, length);
    return ERROR_SUCCESS;
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
