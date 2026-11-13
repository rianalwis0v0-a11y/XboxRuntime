#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <string>
#include "kernel_compat.h"
#include "guest_protocol.h"
#include "guest_api_registry.h"

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
XboxGuestCommandReply Reply(std::uint32_t command, std::uint32_t status, std::uint32_t value = 0) {
    return { XBOX_GUEST_PROTOCOL_MAGIC, command, status, value };
}
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* pipeName = ArgumentValue(argc, argv, L"--pipe");
    const wchar_t* mappingName = ArgumentValue(argc, argv, L"--mapping");
    const wchar_t* eepromPath = ArgumentValue(argc, argv, L"--eeprom");
    const wchar_t* titlePath = ArgumentValue(argc, argv, L"--xbe");
    const std::uint32_t base = NumberArgument(argc, argv, L"--base");
    const std::uint32_t size = NumberArgument(argc, argv, L"--size");
    if (!pipeName || !mappingName || !eepromPath || !base || !size ||
        std::uint64_t(base) + size > 0x80000000ull)
        return ERROR_INVALID_PARAMETER;

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
    if (!mapping) return GetLastError();
    void* image = MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                  size, reinterpret_cast<void*>(uintptr_t(base)));
    if (image != reinterpret_cast<void*>(uintptr_t(base))) {
        const DWORD error = GetLastError();
        if (image) UnmapViewOfFile(image);
        CloseHandle(mapping);
        return error ? error : ERROR_INVALID_ADDRESS;
    }

    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        UnmapViewOfFile(image);
        CloseHandle(mapping);
        return ERROR_PIPE_NOT_CONNECTED;
    }

    std::uint32_t status = ERROR_SUCCESS;
    if (!XboxKernelInitializeEeprom(eepromPath)) status = ERROR_OPEN_FAILED;
    else XboxKernelSetTitlePath(titlePath);

    XboxGuestHello hello{
        XBOX_GUEST_PROTOCOL_MAGIC, XBOX_GUEST_PROTOCOL_VERSION,
        status, base, size
    };
    bool ok = WriteExact(pipe, &hello, sizeof(hello));
    bool importsResolved = false;
    HANDLE guestThread = nullptr;

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
                status = (XboxKernelResolveImports(state) &&
                          XboxKernelProtectImage()) ? ERROR_SUCCESS : ERROR_BAD_EXE_FORMAT;
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
                    CloseHandle(ready);
                }
            }
            const auto reply = Reply(message.command, status);
            ok = WriteExact(pipe, &reply, sizeof(reply));
            if (status == ERROR_TIMEOUT) ExitProcess(status);
        } else if (message.command == XboxGuestLookupApi) {
            const std::uint32_t address = XboxGuestApiLookup(message.symbolName);
            const auto reply = Reply(message.command,
                address ? ERROR_SUCCESS : ERROR_PROC_NOT_FOUND, address);
            ok = WriteExact(pipe, &reply, sizeof(reply));
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
            XboxKernelShutdownTitle();
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
    XboxKernelShutdownTitle();
    XboxKernelFlushEeprom();
    CloseHandle(pipe);
    UnmapViewOfFile(image);
    CloseHandle(mapping);
    return ok ? ERROR_SUCCESS : ERROR_BROKEN_PIPE;
}
