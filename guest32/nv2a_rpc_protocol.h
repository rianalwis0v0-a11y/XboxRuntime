#pragma once
#include <stdint.h>

/* Version 1 guest32 <-> host xbe_loader NV2A RPC wire format.
 * All fields are fixed-width; no process-local pointers or handles cross IPC. */
#define XBOX_NV2A_RPC_MAGIC 0x43525047u /* GPRC */
#define XBOX_NV2A_RPC_VERSION 1u

enum XboxNV2ARpcOpcode {
    XBOX_NV2A_RPC_READ_REGISTER = 1,
    XBOX_NV2A_RPC_WRITE_REGISTER = 2,
    XBOX_NV2A_RPC_SUBMIT_METHOD = 3,
    XBOX_NV2A_RPC_FLUSH = 4
};

#pragma pack(push, 1)
typedef struct XboxNV2ARpcRequest {
    uint32_t magic;
    uint32_t version;
    uint32_t opcode;
    uint32_t payload_size;
    uint32_t request_id;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
} XboxNV2ARpcRequest;

typedef struct XboxNV2ARpcResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t request_id;
    uint32_t status;
    uint64_t value;
} XboxNV2ARpcResponse;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(XboxNV2ARpcRequest) == 36, "NV2A RPC request ABI drift");
static_assert(sizeof(XboxNV2ARpcResponse) == 24, "NV2A RPC response ABI drift");
#endif
