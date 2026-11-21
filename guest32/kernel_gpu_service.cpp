#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "kernel_gpu_service.h"
#include "kernel_gpu_service_protocol.h"

namespace {
HANDLE g_pipe = INVALID_HANDLE_VALUE;
volatile LONG g_lock = 0;
uint32_t g_nextRequestId = 1;

bool ExactRead(void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD received = 0;
        if (!ReadFile(g_pipe, static_cast<uint8_t*>(data) + done,
                      bytes - done, &received, nullptr) || !received)
            return false;
        done += received;
    }
    return true;
}
bool ExactWrite(const void* data, DWORD bytes) {
    DWORD done = 0;
    while (done < bytes) {
        DWORD sent = 0;
        if (!WriteFile(g_pipe, static_cast<const uint8_t*>(data) + done,
                       bytes - done, &sent, nullptr) || !sent)
            return false;
        done += sent;
    }
    return true;
}
void Lock() {
    while (InterlockedCompareExchange(&g_lock, 1, 0) != 0)
        SwitchToThread();
}
void Unlock() { InterlockedExchange(&g_lock, 0); }

bool Call(uint32_t opcode, uint32_t arg0, uint32_t arg1,
          uint32_t arg2, uint32_t arg3, uint64_t* value) {
    if (g_pipe == nullptr || g_pipe == INVALID_HANDLE_VALUE)
        return false;
    Lock();
    XboxGpuRpcRequest request{
        XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION, opcode, 0,
        g_nextRequestId++, arg0, arg1, arg2, arg3
    };
    XboxGpuRpcResponse response{};
    const bool ok = ExactWrite(&request, sizeof(request)) &&
        ExactRead(&response, sizeof(response)) &&
        response.magic == XBOX_GPU_RPC_MAGIC &&
        response.version == XBOX_GPU_RPC_VERSION &&
        response.request_id == request.request_id &&
        response.status == ERROR_SUCCESS;
    if (ok && value) *value = response.value;
    Unlock();
    return ok;
}
bool CallBuffer(uint32_t opcode, uint32_t offset, void* buffer,
                uint32_t length, bool write) {
    if (g_pipe == nullptr || g_pipe == INVALID_HANDLE_VALUE ||
        length > XBOX_GPU_RPC_MAX_TRANSFER || (!buffer && length))
        return false;
    Lock();
    XboxGpuRpcRequest request{
        XBOX_GPU_RPC_MAGIC, XBOX_GPU_RPC_VERSION, opcode,
        write ? length : 0u, g_nextRequestId++, offset, length, 0, 0
    };
    XboxGpuRpcResponse response{};
    bool ok = ExactWrite(&request, sizeof(request));
    if (ok && write && length)
        ok = ExactWrite(buffer, length);
    if (ok) {
        ok = ExactRead(&response, sizeof(response)) &&
            response.magic == XBOX_GPU_RPC_MAGIC &&
            response.version == XBOX_GPU_RPC_VERSION &&
            response.request_id == request.request_id &&
            response.status == ERROR_SUCCESS;
    }
    if (ok && !write && length)
        ok = response.value == length && ExactRead(buffer, length);
    Unlock();
    return ok;
}
}

BOOL XboxKernelGpuServiceInitialize(HANDLE pipe) {
    if (!pipe || pipe == INVALID_HANDLE_VALUE)
        return FALSE;
    g_pipe = pipe;
    uint64_t version = 0;
    if (!Call(XboxGpuRpcPing, 0, 0, 0, 0, &version) ||
        version != XBOX_GPU_RPC_VERSION) {
        g_pipe = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return TRUE;
}
void XboxKernelGpuServiceShutdown(void) {
    Lock();
    g_pipe = INVALID_HANDLE_VALUE;
    Unlock();
}
BOOL XboxKernelGpuServiceConnected(void) {
    return g_pipe != nullptr && g_pipe != INVALID_HANDLE_VALUE;
}
BOOL XboxKernelGpuReadRegister(uint32_t offset, uint32_t width, uint64_t* value) {
    if (!value || (width != 1 && width != 2 && width != 4)) return FALSE;
    return Call(XboxGpuRpcReadRegister, offset, width, 0, 0, value) ? TRUE : FALSE;
}
BOOL XboxKernelGpuWriteRegister(uint32_t offset, uint32_t width, uint64_t value) {
    if (width != 1 && width != 2 && width != 4) return FALSE;
    return Call(XboxGpuRpcWriteRegister, offset, static_cast<uint32_t>(value),
                static_cast<uint32_t>(value >> 32), width, nullptr) ? TRUE : FALSE;
}
BOOL XboxKernelGpuSubmitMethod(uint32_t subchannel, uint32_t method, uint32_t parameter) {
    return Call(XboxGpuRpcSubmitMethod, subchannel, method, parameter, 0, nullptr) ? TRUE : FALSE;
}
BOOL XboxKernelGpuFlush(void) {
    return Call(XboxGpuRpcFlush, 0, 0, 0, 0, nullptr) ? TRUE : FALSE;
}
BOOL XboxKernelGpuReadVram(uint32_t offset, void* destination, uint32_t length) {
    return CallBuffer(XboxGpuRpcReadVram, offset, destination, length, false) ? TRUE : FALSE;
}
BOOL XboxKernelGpuWriteVram(uint32_t offset, const void* source, uint32_t length) {
    return CallBuffer(XboxGpuRpcWriteVram, offset, const_cast<void*>(source), length, true) ? TRUE : FALSE;
}
BOOL XboxKernelGpuReadRamin(uint32_t offset, void* destination, uint32_t length) {
    return CallBuffer(XboxGpuRpcReadRamin, offset, destination, length, false) ? TRUE : FALSE;
}
BOOL XboxKernelGpuWriteRamin(uint32_t offset, const void* source, uint32_t length) {
    return CallBuffer(XboxGpuRpcWriteRamin, offset, const_cast<void*>(source), length, true) ? TRUE : FALSE;
}

extern "C" uint64_t XboxKernelGpuReadRegisterValue(
    uint32_t offset, uint32_t width, int* succeeded) {
    uint64_t value = 0;
    const BOOL ok = XboxKernelGpuReadRegister(offset, width, &value);
    if (succeeded) *succeeded = ok ? 1 : 0;
    return ok ? value : 0;
}
extern "C" BOOL XboxKernelGpuWriteRegisterValue(
    uint32_t offset, uint32_t width, uint64_t value) {
    return XboxKernelGpuWriteRegister(offset, width, value);
}
extern "C" BOOL XboxKernelGpuSubmitMethodValue(
    uint32_t subchannel, uint32_t method, uint32_t parameter) {
    return XboxKernelGpuSubmitMethod(subchannel, method, parameter);
}
extern "C" BOOL XboxKernelGpuFlushValue(void) {
    return XboxKernelGpuFlush();
}
extern "C" BOOL XboxKernelGpuReadLocalMemory(
    BOOL ramin, uint32_t offset, void* destination, uint32_t length) {
    return ramin ? XboxKernelGpuReadRamin(offset, destination, length)
                 : XboxKernelGpuReadVram(offset, destination, length);
}
extern "C" BOOL XboxKernelGpuWriteLocalMemory(
    BOOL ramin, uint32_t offset, const void* source, uint32_t length) {
    return ramin ? XboxKernelGpuWriteRamin(offset, source, length)
                 : XboxKernelGpuWriteVram(offset, source, length);
}
