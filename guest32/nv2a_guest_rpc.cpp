#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "nv2a_guest_rpc.h"
#include "nv2a_rpc_protocol.h"

namespace {
HANDLE g_pipe=nullptr;
volatile LONG g_lock=0;
uint32_t g_nextId=1;
bool ExactRead(void* data,DWORD bytes) {
 DWORD done=0;
 while(done<bytes){DWORD got=0;if(!ReadFile(g_pipe,static_cast<uint8_t*>(data)+done,bytes-done,&got,nullptr)||!got)return false;done+=got;}
 return true;
}
bool ExactWrite(const void* data,DWORD bytes) {
 DWORD done=0;
 while(done<bytes){DWORD sent=0;if(!WriteFile(g_pipe,static_cast<const uint8_t*>(data)+done,bytes-done,&sent,nullptr)||!sent)return false;done+=sent;}
 return true;
}
bool Call(uint32_t op,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint64_t* value) {
 if(!g_pipe||g_pipe==INVALID_HANDLE_VALUE)return false;
 while(InterlockedCompareExchange(&g_lock,1,0)!=0)SwitchToThread();
 XboxNV2ARpcRequest q{XBOX_NV2A_RPC_MAGIC,XBOX_NV2A_RPC_VERSION,op,0,g_nextId++,a0,a1,a2,a3};
 XboxNV2ARpcResponse r{};
 const bool ok=ExactWrite(&q,sizeof(q))&&ExactRead(&r,sizeof(r))&&r.magic==XBOX_NV2A_RPC_MAGIC&&r.version==XBOX_NV2A_RPC_VERSION&&r.request_id==q.request_id&&r.status==0;
 InterlockedExchange(&g_lock,0);
 if(ok&&value)*value=r.value;
 return ok;
}
}
void XboxGuestNV2ASetPipe(HANDLE pipe){g_pipe=pipe;}
uint64_t XboxGuestNV2AReadRegister(uint32_t offset,uint32_t size,int* ok){uint64_t v=0;bool good=Call(1,offset,size,0,0,&v);if(ok)*ok=good?1:0;return good?v:0;}
int XboxGuestNV2AWriteRegister(uint32_t offset,uint64_t value,uint32_t size){return Call(2,offset,uint32_t(value),uint32_t(value>>32),size,nullptr)?1:0;}
int XboxGuestNV2ASubmitMethod(uint32_t subchannel,uint32_t method,uint32_t parameter){return Call(3,subchannel,method,parameter,0,nullptr)?1:0;}
int XboxGuestNV2AFlush(void){return Call(4,0,0,0,0,nullptr)?1:0;}
