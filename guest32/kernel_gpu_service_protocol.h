#pragma once
#include <stdint.h>

/*
 * Combined-kernel NV2A service wire ABI.
 *
 * This is a transport contract, not a host pointer ABI. All fields are fixed
 * width; guest virtual addresses, host pointers, HANDLEs, and C++ objects must
 * never be placed in these messages. The control pipe in guest_protocol.h is
 * a separate channel and must not be multiplexed with GPU traffic.
 */
#define XBOX_GPU_RPC_MAGIC 0x43525047u /* "GPRC" */
#define XBOX_GPU_RPC_VERSION 1u
#define XBOX_GPU_RPC_MAX_TRANSFER (64u * 1024u)

enum XboxGpuRpcOpcode {
    XboxGpuRpcPing = 0,
    XboxGpuRpcReadRegister = 1,
    XboxGpuRpcWriteRegister = 2,
    XboxGpuRpcSubmitMethod = 3,
    XboxGpuRpcFlush = 4,
    XboxGpuRpcReadVram = 5,
    XboxGpuRpcWriteVram = 6,
    XboxGpuRpcReadRamin = 7,
    XboxGpuRpcWriteRamin = 8
};

#pragma pack(push, 1)
typedef struct XboxGpuRpcRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t opcode;
    uint32_t payload_size;
    uint32_t request_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} XboxGpuRpcRequest;

typedef struct XboxGpuRpcResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t request_id;
    uint32_t status;
    uint64_t value;
} XboxGpuRpcResponse;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(XboxGpuRpcRequest) == 36, "GPU RPC request ABI drift");
static_assert(sizeof(XboxGpuRpcResponse) == 24, "GPU RPC response ABI drift");
#endif
