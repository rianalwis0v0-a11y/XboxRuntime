#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <string>
#include "kernel_compat.h"
#include "kernel_gpu_service.h"
#include "kernel_gpu_mmio.h"
#include "kernel_memory.h"
#include "kernel_physical_memory.h"
#include "kernel_gpu_service_protocol.h"
#include "guest_protocol.h"
#include "kernel_storage_mounts.h"

namespace {
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
std::uint32_t g_tlsAddress = 0;
const wchar_t* ArgumentValue(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wcscmp(argv[i], name) == 0) return argv[i + 1];
    return nullptr;
}
std::uint32_t NumberArgument(int argc, wchar_t** argv, const wchar_t* name) {
    const wchar_t* value = ArgumentValue(argc, argv, name);
    if (!value || !*value) return 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 0);
    return (end && *end == L'\0' && parsed <= 0xFFFFFFFFul)
        ? static_cast<std::uint32_t>(parsed) : 0;
}
XboxGuestCommandReply Reply(std::uint32_t command, std::uint32_t status) {
    return { XBOX_GUEST_PROTOCOL_MAGIC, command, status };
}
void* MapGuestPhysicalMemoryView(HANDLE mapping) {
    // NXDK/pbkit treats contiguous-memory pointers as physical addresses:
    // it masks them with 0x03FFFFFF and ORs them with the 0xF0000000 AGP
    // remap aperture. Map the 64 MiB section at a 256 MiB-aligned guest VA
    // so both transformations preserve the section-relative physical offset.
    constexpr std::uintptr_t alignment = 0x10000000u;
    constexpr std::uintptr_t firstCandidate = alignment;
    constexpr std::uintptr_t lastCandidate = 0x70000000u;
    for (std::uintptr_t base = firstCandidate; base <= lastCandidate;
         base += alignment) {
        void* requested = reinterpret_cast<void*>(base);
        void* view = MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
            XBOX_GUEST_PHYSICAL_RAM_SIZE, requested);
        if (view == requested)
            return view;
        if (view)
            UnmapViewOfFile(view);
    }
    return nullptr;
}
struct GpuMemoryMappingGuard {
    HANDLE mapping = nullptr;
    void* view = nullptr;
    bool initialized = false;
    HANDLE physicalMapping = nullptr;
    void* physicalView = nullptr;
    bool physicalInitialized = false;
    ~GpuMemoryMappingGuard() {
        if (physicalInitialized) XboxKernelPhysicalMemoryShutdown();
        if (physicalView) UnmapViewOfFile(physicalView);
        if (physicalMapping) CloseHandle(physicalMapping);
        if (initialized) XboxKernelGpuMemoryShutdown();
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
    }
};

}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* pipeName = ArgumentValue(argc, argv, L"--pipe");
    const wchar_t* gpuPipeName = ArgumentValue(argc, argv, L"--gpu-pipe");
    const wchar_t* gpuMemoryName = ArgumentValue(argc, argv, L"--gpu-memory");
    const wchar_t* physicalMemoryName = ArgumentValue(argc, argv, L"--physical-memory");
    const wchar_t* mappingName = ArgumentValue(argc, argv, L"--mapping");
    const wchar_t* eepromPath = ArgumentValue(argc, argv, L"--eeprom");
    const wchar_t* titlePath = ArgumentValue(argc, argv, L"--xbe");
    const std::uint32_t base = NumberArgument(argc, argv, L"--base");
    const std::uint32_t size = NumberArgument(argc, argv, L"--size");
    if (!pipeName || !gpuPipeName || !gpuMemoryName || !physicalMemoryName ||
        !mappingName || !eepromPath || !base || !size ||
        std::uint64_t(base) + size > 0x80000000ull)
        return ERROR_INVALID_PARAMETER;

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
    if (!mapping) return GetLastError();
    void* image = MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS | FILE_MAP_EXECUTE, 0, 0,
                                  size, reinterpret_cast<void*>(uintptr_t(base)));
    if (image != reinterpret_cast<void*>(uintptr_t(base))) {
        const DWORD error = GetLastError();
        if (image) UnmapViewOfFile(image);
        CloseHandle(mapping);
        return error ? error : ERROR_INVALID_ADDRESS;
    }

    GpuMemoryMappingGuard gpuMemory;
    gpuMemory.mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, gpuMemoryName);
    if (!gpuMemory.mapping) {
        const DWORD error = GetLastError();
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return error;
    }
    gpuMemory.view = MapViewOfFile(gpuMemory.mapping, FILE_MAP_ALL_ACCESS,
        0, 0, XBOX_GPU_SHARED_MEMORY_SIZE);
    if (!gpuMemory.view) {
        const DWORD error = GetLastError();
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!XboxKernelGpuMemoryInitialize(gpuMemory.view, XBOX_GPU_SHARED_MEMORY_SIZE)) {
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return ERROR_NOT_READY;
    }
    gpuMemory.initialized = true;

    gpuMemory.physicalMapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE, physicalMemoryName);
    if (!gpuMemory.physicalMapping) return GetLastError();
    gpuMemory.physicalView = MapGuestPhysicalMemoryView(gpuMemory.physicalMapping);
    if (!gpuMemory.physicalView) return GetLastError();
    if ((reinterpret_cast<std::uintptr_t>(gpuMemory.physicalView) & 0x0FFFFFFFu) != 0)
        return ERROR_INVALID_ADDRESS;
    if (!XboxKernelPhysicalMemoryInitialize(gpuMemory.physicalView,
            XBOX_GUEST_PHYSICAL_RAM_BASE, XBOX_GUEST_PHYSICAL_RAM_SIZE))
        return ERROR_NOT_READY;
    gpuMemory.physicalInitialized = true;
    std::uint32_t translatedPhysicalBase = 0xFFFFFFFFu;
    if (!XboxKernelTranslateGuestVirtualToPhysical(gpuMemory.physicalView, 4u,
            XboxGuestMemoryRead | XboxGuestMemoryWrite, &translatedPhysicalBase) ||
        translatedPhysicalBase != XBOX_GUEST_PHYSICAL_RAM_BASE)
        return ERROR_INVALID_ADDRESS;
    wchar_t physicalMapLog[128]{};
    swprintf_s(physicalMapLog, _countof(physicalMapLog),
        L"[XboxRuntime] Guest physical RAM mapped at 0x%08X (64 MiB, pbkit-compatible).\\n",
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gpuMemory.physicalView)));
    OutputDebugStringW(physicalMapLog);

    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return ERROR_PIPE_NOT_CONNECTED;
    }

    HANDLE gpuPipe = CreateFileW(gpuPipeName, GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
    if (gpuPipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        CloseHandle(pipe);
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return error ? error : ERROR_PIPE_NOT_CONNECTED;
    }
    if (!XboxKernelGpuServiceInitialize(gpuPipe)) {
        XboxKernelGpuServiceShutdown();
        CloseHandle(gpuPipe);
        CloseHandle(pipe);
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return ERROR_NOT_READY;
    }

    if (!XboxKernelGpuMmioInitialize()) {
        XboxKernelGpuServiceShutdown();
        CloseHandle(gpuPipe);
        CloseHandle(pipe);
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return ERROR_NOT_READY;
    }

    std::uint32_t status = ERROR_SUCCESS;
    if (!XboxKernelInitializeEeprom(eepromPath)) status = ERROR_OPEN_FAILED;
    else {
        XboxKernelSetTitlePath(titlePath);
        if (!XboxKernelMountConfiguredStorage()) status = ERROR_INVALID_DATA;
    }

    XboxGuestHello hello{
        XBOX_GUEST_PROTOCOL_MAGIC, XBOX_GUEST_PROTOCOL_VERSION,
        status, base, size
    };
    bool ok = WriteExact(pipe, &hello, sizeof(hello));
    bool importsResolved = false;
    HANDLE guestThread = nullptr;
    HANDLE guestExitEvent = nullptr;

    while (ok) {
        XboxGuestCommandMessage message{};
        if (!ReadExact(pipe, &message, sizeof(message))) break;
        if (message.magic != XBOX_GUEST_PROTOCOL_MAGIC) break;

        if (message.command == XboxGuestResolveImports) {
            if (guestThread || importsResolved ||
                message.imageBase != base || message.imageSize != size) {
                status = ERROR_INVALID_STATE;
            } else {
                XboxKernelState state{
                    message.imageBase, message.imageSize, message.headerSize,
                    message.entryPoint, message.sectionCount, message.tlsAddress,
                    message.nonKernelImportDirectoryAddress,
                    message.kernelThunkAddress
                };
                g_tlsAddress = message.tlsAddress;
                if (!XboxKernelResolveImports(state)) {
                    status = ERROR_INVALID_DATA;
                    OutputDebugStringW(L"[XboxKernel] Guest import resolution failed.\\n");
                } else if (!XboxKernelProtectImage()) {
                    status = ERROR_NOACCESS;
                    OutputDebugStringW(L"[XboxKernel] Guest image protection failed.\\n");
                } else {
                    status = ERROR_SUCCESS;
                }
                importsResolved = status == ERROR_SUCCESS;
            }
            const auto reply = Reply(message.command, status);
            ok = WriteExact(pipe, &reply, sizeof(reply));
        } else if (message.command == XboxGuestStart) {
            if (!importsResolved || guestThread) {
                status = ERROR_INVALID_STATE;
            } else {
                HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!ready) {
                    status = GetLastError();
                } else {
                    if (!guestExitEvent)
                        guestExitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    if (!guestExitEvent) {
                        status = GetLastError();
                    } else {
                        ResetEvent(guestExitEvent);
                        XboxKernelSetGuestExitEvent(guestExitEvent);
                        XboxKernelSetStartupEvent(ready);
                        guestThread = CreateThread(nullptr, 0, XboxKernelStartXbe,
                                                   nullptr, 0, nullptr);
                        if (!guestThread) {
                            status = GetLastError();
                        } else {
                            HANDLE waits[2] = { ready, guestThread };
                            const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 10000);
                            if (wait == WAIT_OBJECT_0) {
                                status = ERROR_SUCCESS;
                            } else if (wait == WAIT_OBJECT_0 + 1) {
                                DWORD exitCode = ERROR_GEN_FAILURE;
                                GetExitCodeThread(guestThread, &exitCode);
                                status = exitCode ? exitCode : ERROR_PROCESS_ABORTED;
                                CloseHandle(guestThread);
                                guestThread = nullptr;
                            } else {
                                status = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
                            }
                        }
                        XboxKernelSetStartupEvent(nullptr);
                        if (status != ERROR_SUCCESS) {
                            XboxKernelSetGuestExitEvent(nullptr);
                            CloseHandle(guestExitEvent);
                            guestExitEvent = nullptr;
                        }
                    }
                    CloseHandle(ready);
                }
            }
            const auto reply = Reply(message.command, status);
            ok = WriteExact(pipe, &reply, sizeof(reply));
            if (status == ERROR_TIMEOUT) ExitProcess(status);
        } else if (message.command == XboxGuestWaitForExit) {
            XboxGuestExitReply exitReply{
                XBOX_GUEST_PROTOCOL_MAGIC, XboxGuestWaitForExit,
                ERROR_INVALID_STATE, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0
            };
            if (guestThread) {
                const DWORD timeout = message.imageBase ? message.imageBase : INFINITE;
                const DWORD waitResult = WaitForSingleObject(guestThread, timeout);
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD exitCode = ERROR_GEN_FAILURE;
                    if (GetExitCodeThread(guestThread, &exitCode)) {
                        CloseHandle(guestThread);
                        guestThread = nullptr;
                        bool tlsCleared = false;
                        if (g_tlsAddress) {
                            struct TlsDirectory {
                                std::uint32_t start, end, index, callbacks,
                                              zeroFill, characteristics;
                            };
                            if (g_tlsAddress >= base &&
                                std::uint64_t(g_tlsAddress) + sizeof(TlsDirectory) <=
                                    std::uint64_t(base) + size) {
                                const auto* tls = reinterpret_cast<const TlsDirectory*>(
                                    uintptr_t(g_tlsAddress));
                                if (tls->index >= base &&
                                    std::uint64_t(tls->index) + sizeof(std::uint32_t) <=
                                        std::uint64_t(base) + size) {
                                    const auto* index = reinterpret_cast<const std::uint32_t*>(
                                        uintptr_t(tls->index));
                                    tlsCleared = *index == TLS_OUT_OF_INDEXES;
                                }
                            }
                        }
                        exitReply.status = ERROR_SUCCESS;
                        exitReply.exitCode = exitCode;
                        exitReply.tlsIndexCleared = tlsCleared ? 1u : 0u;
                        XboxKernelGetLastExceptionDiagnostic(
                            nullptr, &exitReply.exceptionInstruction,
                            &exitReply.accessType, &exitReply.faultAddress,
                            &exitReply.faultProtection, &exitReply.faultState,
                            &exitReply.eax, &exitReply.ebx, &exitReply.ecx, &exitReply.edx,
                            &exitReply.esi, &exitReply.edi, &exitReply.ebp, &exitReply.esp);
                        XboxKernelSetGuestExitEvent(nullptr);
                        if (guestExitEvent) {
                            CloseHandle(guestExitEvent);
                            guestExitEvent = nullptr;
                        }
                        XboxKernelShutdownTitle();
                    } else {
                        exitReply.status = GetLastError();
                    }
                } else if (waitResult == WAIT_TIMEOUT) {
                    exitReply.status = ERROR_TIMEOUT;
                } else {
                    exitReply.status = GetLastError();
                }
            }
            ok = WriteExact(pipe, &exitReply, sizeof(exitReply));
        } else if (message.command == XboxGuestStop) {
            const bool threadRunning = guestThread &&
                WaitForSingleObject(guestThread, 0) == WAIT_TIMEOUT;
            const auto reply = Reply(message.command, ERROR_SUCCESS);
            WriteExact(pipe, &reply, sizeof(reply));
            if (threadRunning) {
                // A title can run indefinitely. Process teardown is the safe
                // isolation boundary; never free its code or TLS underneath it.
                ExitProcess(ERROR_SUCCESS);
            }
            if (guestThread) {
                CloseHandle(guestThread);
                guestThread = nullptr;
            }
            XboxKernelSetGuestExitEvent(nullptr);
            if (guestExitEvent) {
                CloseHandle(guestExitEvent);
                guestExitEvent = nullptr;
            }
            XboxKernelShutdownTitle();
            XboxKernelGpuMmioShutdown();
            XboxKernelGpuServiceShutdown();
            CloseHandle(gpuPipe);
            gpuPipe = INVALID_HANDLE_VALUE;
            XboxKernelUnmountConfiguredStorage();
            XboxKernelFlushEeprom();
            break;
        } else {
            const auto reply = Reply(message.command, ERROR_INVALID_FUNCTION);
            ok = WriteExact(pipe, &reply, sizeof(reply));
        }
    }

    if (guestThread) {
        if (WaitForSingleObject(guestThread, 0) == WAIT_OBJECT_0)
            CloseHandle(guestThread);
        else
            ExitProcess(ERROR_OPERATION_ABORTED);
    }
    XboxKernelSetGuestExitEvent(nullptr);
    if (guestExitEvent) CloseHandle(guestExitEvent);
    XboxKernelShutdownTitle();
    XboxKernelGpuMmioShutdown();
    XboxKernelGpuServiceShutdown();
    if (gpuPipe != INVALID_HANDLE_VALUE) CloseHandle(gpuPipe);
    XboxKernelUnmountConfiguredStorage();
    XboxKernelFlushEeprom();
    CloseHandle(pipe);
    UnmapViewOfFile(image);
    CloseHandle(mapping);
    return ok ? ERROR_SUCCESS : ERROR_BROKEN_PIPE;
}
