#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../guest32/kernel_gpu_service.h"
#include "../guest32/kernel_gpu_service_protocol.h"
#include "../guest32/kernel_gpu_mmio.h"

namespace {
struct ServerState { HANDLE pipe = INVALID_HANDLE_VALUE; bool valid = true; };
bool ReadExact(HANDLE pipe, void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD got = 0;
        if (!ReadFile(pipe, static_cast<uint8_t*>(data) + done, bytes - done,
                      &got, nullptr) || !got) return false;
        done += got;
    }
    return true;
}
bool WriteExact(HANDLE pipe, const void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD sent = 0;
        if (!WriteFile(pipe, static_cast<const uint8_t*>(data) + done,
                       bytes - done, &sent, nullptr) || !sent) return false;
        done += sent;
    }
    return true;
}
DWORD WINAPI Server(void* raw) {
    auto* state = static_cast<ServerState*>(raw);
    if (!ConnectNamedPipe(state->pipe, nullptr) &&
        GetLastError() != ERROR_PIPE_CONNECTED) return 1;
    for (uint32_t id = 1; id <= 3; ++id) {
        XboxGpuRpcRequest request{};
        if (!ReadExact(state->pipe, &request, sizeof(request))) return 2;
        if (request.magic != XBOX_GPU_RPC_MAGIC ||
            request.version != XBOX_GPU_RPC_VERSION ||
            request.request_id != id) state->valid = false;
        XboxGpuRpcResponse response{
            XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION,
            request.request_id, ERROR_SUCCESS, 0
        };
        if (id == 1) {
            if (request.opcode != XboxGpuRpcPing) state->valid = false;
            response.value = XBOX_GPU_RPC_VERSION;
        } else if (id == 2) {
            if (request.opcode != XboxGpuRpcReadRegister ||
                request.arg0 != 0x10u || request.arg1 != 4u)
                state->valid = false;
            response.value = 0x12345678u;
        } else {
            if (request.opcode != XboxGpuRpcWriteRegister ||
                request.arg0 != 0x30u || request.arg1 != 0xAABBCCDDu ||
                request.arg3 != 4u)
                state->valid = false;
        }
        if (!WriteExact(state->pipe, &response, sizeof(response))) return 3;
    }
    return state->valid ? 0 : 4;
}
}

int main() {
    wchar_t name[128]{};
    swprintf_s(name, L"\\\\.\\pipe\\XboxGpuMmioSmoke-%lu-%llu",
               GetCurrentProcessId(), GetTickCount64());
    ServerState serverState{};
    serverState.pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    if (serverState.pipe == INVALID_HANDLE_VALUE) return 1;
    HANDLE server = CreateThread(nullptr, 0, Server, &serverState, 0, nullptr);
    if (!server) { CloseHandle(serverState.pipe); return 2; }
    HANDLE client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (client == INVALID_HANDLE_VALUE) return 3;
    if (!XboxKernelGpuServiceInitialize(client) || !XboxKernelGpuMmioInitialize())
        return 4;

    uint8_t readCode[5] = { 0xA1, 0x10, 0x00, 0x00, 0xFD }; // mov eax,[0xFD000010]
    CONTEXT readContext{};
    readContext.Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(readCode));
    EXCEPTION_RECORD readRecord{};
    readRecord.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    readRecord.NumberParameters = 2;
    readRecord.ExceptionInformation[0] = 0;
    readRecord.ExceptionInformation[1] = 0xFD000010u;
    EXCEPTION_POINTERS readException{ &readRecord, &readContext };
    const LONG readResult = XboxKernelGpuMmioExceptionHandler(&readException);

    uint8_t writeCode[3] = { 0x89, 0x41, 0x10 }; // mov [ecx+0x10],eax
    CONTEXT writeContext{};
    writeContext.Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(writeCode));
    writeContext.Eax = 0xAABBCCDDu;
    writeContext.Ecx = 0xFD000020u;
    EXCEPTION_RECORD writeRecord{};
    writeRecord.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    writeRecord.NumberParameters = 2;
    writeRecord.ExceptionInformation[0] = 1;
    writeRecord.ExceptionInformation[1] = 0xFD000030u;
    EXCEPTION_POINTERS writeException{ &writeRecord, &writeContext };
    const LONG writeResult = XboxKernelGpuMmioExceptionHandler(&writeException);

    XboxKernelGpuMmioShutdown();
    XboxKernelGpuServiceShutdown();
    CloseHandle(client);
    const DWORD wait = WaitForSingleObject(server, 5000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeThread(server, &exitCode);
    DisconnectNamedPipe(serverState.pipe);
    CloseHandle(serverState.pipe);
    CloseHandle(server);

    if (readResult != EXCEPTION_CONTINUE_EXECUTION ||
        readContext.Eax != 0x12345678u ||
        readContext.Eip != static_cast<DWORD>(reinterpret_cast<uintptr_t>(readCode + 5)) ||
        writeResult != EXCEPTION_CONTINUE_EXECUTION ||
        writeContext.Eip != static_cast<DWORD>(reinterpret_cast<uintptr_t>(writeCode + 3)) ||
        wait != WAIT_OBJECT_0 || exitCode != 0 || !serverState.valid) {
        std::fprintf(stderr, "FAIL: NV2A MMIO decoder/RPC bridge\n");
        return 5;
    }
    std::puts("PASS: NV2A MMIO absolute read and register-indirect write cross GPU RPC");
    return 0;
}
