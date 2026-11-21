#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <thread>
#include "kernel_gpu_service.h"
#include "kernel_gpu_service_protocol.h"

namespace {
bool ReadExact(HANDLE pipe, void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD got = 0;
        if (!ReadFile(pipe, static_cast<std::uint8_t*>(data) + done,
                      bytes - done, &got, nullptr) || !got) return false;
        done += got;
    }
    return true;
}
bool WriteExact(HANDLE pipe, const void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD sent = 0;
        if (!WriteFile(pipe, static_cast<const std::uint8_t*>(data) + done,
                       bytes - done, &sent, nullptr) || !sent) return false;
        done += sent;
    }
    return true;
}
bool Exchange(HANDLE pipe, std::uint32_t id, std::uint32_t opcode,
              std::uint32_t arg0, std::uint32_t arg1,
              std::uint32_t arg2, std::uint32_t arg3,
              const std::uint8_t* incoming, std::uint32_t incomingSize,
              std::uint64_t value, const std::uint8_t* outgoing,
              std::uint32_t outgoingSize) {
    XboxGpuRpcRequest request{};
    if (!ReadExact(pipe, &request, sizeof(request))) return false;
    if (request.magic != XBOX_GPU_RPC_MAGIC ||
        request.version != XBOX_GPU_RPC_VERSION ||
        request.request_id != id || request.opcode != opcode ||
        request.arg0 != arg0 || request.arg1 != arg1 ||
        request.arg2 != arg2 || request.arg3 != arg3 ||
        request.payload_size != incomingSize) return false;
    std::uint8_t payload[XBOX_GPU_RPC_MAX_TRANSFER]{};
    if (incomingSize) {
        if (!ReadExact(pipe, payload, incomingSize) ||
            std::memcmp(payload, incoming, incomingSize) != 0) return false;
    }
    XboxGpuRpcResponse response{
        XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION, id, ERROR_SUCCESS, value
    };
    if (!WriteExact(pipe, &response, sizeof(response))) return false;
    return !outgoingSize || WriteExact(pipe, outgoing, outgoingSize);
}
DWORD WINAPI ServerMain(void* raw) {
    HANDLE pipe = static_cast<HANDLE>(raw);
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
        return 1;
    const std::uint8_t writeData[] = {0x10, 0x20, 0x30, 0x40};
    const std::uint8_t readData[] = {0xA1, 0xB2, 0xC3, 0xD4};
    const std::uint8_t raminData[] = {0x55, 0x66, 0x77};
    bool ok =
        Exchange(pipe, 1, XboxGpuRpcPing, 0, 0, 0, 0, nullptr, 0,
                 XBOX_GPU_RPC_VERSION, nullptr, 0) &&
        Exchange(pipe, 2, XboxGpuRpcReadRegister, 0x100, 4, 0, 0,
                 nullptr, 0, 0x12345678u, nullptr, 0) &&
        Exchange(pipe, 3, XboxGpuRpcWriteRegister, 0x104, 0x89ABCDEFu,
                 0, 4, nullptr, 0, 0, nullptr, 0) &&
        Exchange(pipe, 4, XboxGpuRpcSubmitMethod, 3, 0x180, 0xDEADBEEFu,
                 0, nullptr, 0, 0, nullptr, 0) &&
        Exchange(pipe, 5, XboxGpuRpcFlush, 0, 0, 0, 0,
                 nullptr, 0, 0, nullptr, 0) &&
        Exchange(pipe, 6, XboxGpuRpcReadVram, 0x200, sizeof(readData), 0, 0,
                 nullptr, 0, sizeof(readData), readData, sizeof(readData)) &&
        Exchange(pipe, 7, XboxGpuRpcWriteVram, 0x300, sizeof(writeData), 0, 0,
                 writeData, sizeof(writeData), 0, nullptr, 0) &&
        Exchange(pipe, 8, XboxGpuRpcReadRamin, 0x400, sizeof(raminData), 0, 0,
                 nullptr, 0, sizeof(raminData), raminData, sizeof(raminData)) &&
        Exchange(pipe, 9, XboxGpuRpcWriteRamin, 0x500, sizeof(writeData), 0, 0,
                 writeData, sizeof(writeData), 0, nullptr, 0);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    return ok ? 0 : 2;
}
}

int main() {
    wchar_t name[128]{};
    swprintf_s(name, _countof(name), L"\\\\.\\pipe\\XboxRuntimeGpuSmoke_%lu",
               GetCurrentProcessId());
    HANDLE server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 5000, nullptr);
    if (server == INVALID_HANDLE_VALUE) return 1;

    HANDLE thread = CreateThread(nullptr, 0, ServerMain, server, 0, nullptr);
    if (!thread) { CloseHandle(server); return 2; }
    HANDLE client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (client == INVALID_HANDLE_VALUE) {
        TerminateThread(thread, ERROR_PIPE_NOT_CONNECTED);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread); CloseHandle(server);
        return 3;
    }

    bool ok = XboxKernelGpuServiceInitialize(client) != FALSE;
    std::uint64_t value = 0;
    const std::uint8_t expectedVram[] = {0xA1, 0xB2, 0xC3, 0xD4};
    const std::uint8_t expectedRamin[] = {0x55, 0x66, 0x77};
    const std::uint8_t writeData[] = {0x10, 0x20, 0x30, 0x40};
    std::uint8_t buffer[4]{};
    ok = ok && XboxKernelGpuServiceConnected();
    ok = ok && XboxKernelGpuReadRegister(0x100, 4, &value) &&
         value == 0x12345678u;
    ok = ok && XboxKernelGpuWriteRegister(0x104, 4, 0x89ABCDEFu);
    ok = ok && XboxKernelGpuSubmitMethod(3, 0x180, 0xDEADBEEFu);
    ok = ok && XboxKernelGpuFlush();
    ok = ok && XboxKernelGpuReadVram(0x200, buffer, sizeof(buffer)) &&
         std::memcmp(buffer, expectedVram, sizeof(buffer)) == 0;
    ok = ok && XboxKernelGpuWriteVram(0x300, writeData, sizeof(writeData));
    std::memset(buffer, 0, sizeof(buffer));
    ok = ok && XboxKernelGpuReadRamin(0x400, buffer, 3) &&
         std::memcmp(buffer, expectedRamin, 3) == 0;
    ok = ok && XboxKernelGpuWriteRamin(0x500, writeData, sizeof(writeData));
    XboxKernelGpuServiceShutdown();
    CloseHandle(client);

    WaitForSingleObject(thread, 10000);
    DWORD serverResult = 1;
    GetExitCodeThread(thread, &serverResult);
    CloseHandle(thread);
    CloseHandle(server);
    return ok && serverResult == 0 ? 0 : 4;
}
