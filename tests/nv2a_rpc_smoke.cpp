#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include "nv2a_guest_rpc.h"
#include "nv2a_rpc_protocol.h"

namespace {
bool ReadExact(HANDLE pipe,void* data,DWORD size) {
 DWORD done=0;
 while(done<size){DWORD got=0;if(!ReadFile(pipe,static_cast<uint8_t*>(data)+done,size-done,&got,nullptr)||!got)return false;done+=got;}
 return true;
}
bool WriteExact(HANDLE pipe,const void* data,DWORD size) {
 DWORD done=0;
 while(done<size){DWORD sent=0;if(!WriteFile(pipe,static_cast<const uint8_t*>(data)+done,size-done,&sent,nullptr)||!sent)return false;done+=sent;}
 return true;
}
struct ServerState { HANDLE pipe=nullptr; bool ok=true; };
DWORD WINAPI RpcServer(void* raw) {
 auto* state=static_cast<ServerState*>(raw);
 if(!ConnectNamedPipe(state->pipe,nullptr)&&GetLastError()!=ERROR_PIPE_CONNECTED){state->ok=false;return 1;}
 for(uint32_t expected=1;expected<=8;++expected) {
  XboxNV2ARpcRequest q{};
  if(!ReadExact(state->pipe,&q,sizeof(q))){state->ok=false;return 2;}
  if(q.magic!=XBOX_NV2A_RPC_MAGIC||q.version!=XBOX_NV2A_RPC_VERSION||q.request_id!=expected){state->ok=false;return 3;}
  XboxNV2ARpcResponse a{XBOX_NV2A_RPC_MAGIC,XBOX_NV2A_RPC_VERSION,q.request_id,0,0};
  uint8_t bytes[8]{};
  switch(expected) {
  case 1:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_READ_REGISTER&&q.payload_size==0&&q.arg0==0x100u&&q.arg1==4u;
   a.value=UINT64_C(0x123456789ABCDEF0);break;
  case 2:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_WRITE_REGISTER&&q.payload_size==0&&q.arg0==0x200u&&q.arg1==0x55667788u&&q.arg2==0x11223344u&&q.arg3==4u;break;
  case 3:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_SUBMIT_METHOD&&q.payload_size==0&&q.arg0==2u&&q.arg1==0x1D94u&&q.arg2==0xDEADBEEFu;break;
  case 4:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_FLUSH&&q.payload_size==0;break;
  case 5:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_READ_VRAM&&q.payload_size==0&&q.arg0==0x80u&&q.arg1==5u;
   a.value=5;bytes[0]=1;bytes[1]=2;bytes[2]=3;bytes[3]=4;bytes[4]=5;break;
  case 6:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_WRITE_VRAM&&q.payload_size==4&&q.arg0==0x90u&&q.arg1==4u;
   if(q.payload_size!=4||!ReadExact(state->pipe,bytes,4)){state->ok=false;return 4;}
   state->ok=state->ok&&bytes[0]==6&&bytes[1]==7&&bytes[2]==8&&bytes[3]==9;break;
  case 7:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_READ_RAMIN&&q.payload_size==0&&q.arg0==0x100u&&q.arg1==3u;
   a.value=3;bytes[0]=0xA1;bytes[1]=0xA2;bytes[2]=0xA3;break;
  case 8:
   state->ok=state->ok&&q.opcode==XBOX_NV2A_RPC_WRITE_RAMIN&&q.payload_size==3&&q.arg0==0x200u&&q.arg1==3u;
   if(q.payload_size!=3||!ReadExact(state->pipe,bytes,3)){state->ok=false;return 5;}
   state->ok=state->ok&&bytes[0]==0xB1&&bytes[1]==0xB2&&bytes[2]==0xB3;break;
  }
  if(!state->ok){a.status=1;}
  if(!WriteExact(state->pipe,&a,sizeof(a)))return 6;
  if((expected==5||expected==7)&&!WriteExact(state->pipe,bytes,q.arg1))return 7;
 }
 return state->ok?0:8;
}
}

int main() {
 wchar_t pipeName[128]{};
 swprintf_s(pipeName,L"\\\\.\\pipe\\XboxRuntimeNv2aRpcSmoke-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
 ServerState state{};
 state.pipe=CreateNamedPipeW(pipeName,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,65536,65536,0,nullptr);
 if(state.pipe==INVALID_HANDLE_VALUE){std::cerr<<"FAIL: create RPC test pipe\n";return 1;}
 HANDLE server=CreateThread(nullptr,0,RpcServer,&state,0,nullptr);
 if(!server){CloseHandle(state.pipe);return 1;}
 HANDLE client=CreateFileW(pipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
 if(client==INVALID_HANDLE_VALUE){CloseHandle(state.pipe);WaitForSingleObject(server,1000);CloseHandle(server);return 1;}
 XboxGuestNV2ASetPipe(client);
 int readOk=0;
 const uint64_t registerValue=XboxGuestNV2AReadRegister(0x100u,4u,&readOk);
 const int writeOk=XboxGuestNV2AWriteRegister(0x200u,UINT64_C(0x1122334455667788),4u);
 const int submitOk=XboxGuestNV2ASubmitMethod(2u,0x1D94u,0xDEADBEEFu);
 const int flushOk=XboxGuestNV2AFlush();
 uint8_t vramRead[5]{},vramWrite[4]={6,7,8,9},raminRead[3]{},raminWrite[3]={0xB1,0xB2,0xB3};
 const int readVramOk=XboxGuestNV2AReadVRAM(0x80u,vramRead,sizeof(vramRead));
 const int writeVramOk=XboxGuestNV2AWriteVRAM(0x90u,vramWrite,sizeof(vramWrite));
 const int readRaminOk=XboxGuestNV2AReadRAMIN(0x100u,raminRead,sizeof(raminRead));
 const int writeRaminOk=XboxGuestNV2AWriteRAMIN(0x200u,raminWrite,sizeof(raminWrite));
 const int oversizedRejected=!XboxGuestNV2AWriteVRAM(0,nullptr,XBOX_NV2A_RPC_MAX_TRANSFER+1u);
 XboxGuestNV2ASetPipe(nullptr);
 const DWORD wait=WaitForSingleObject(server,5000);
 DWORD serverExit=1;if(wait==WAIT_OBJECT_0)GetExitCodeThread(server,&serverExit);
 CloseHandle(client);DisconnectNamedPipe(state.pipe);CloseHandle(state.pipe);CloseHandle(server);
 const bool payloads=vramRead[0]==1&&vramRead[4]==5&&raminRead[0]==0xA1&&raminRead[2]==0xA3;
 if(!readOk||registerValue!=UINT64_C(0x123456789ABCDEF0)||!writeOk||!submitOk||!flushOk||
    !readVramOk||!writeVramOk||!readRaminOk||!writeRaminOk||!oversizedRejected||
    !payloads||wait!=WAIT_OBJECT_0||serverExit!=0||!state.ok) {
  std::cerr<<"FAIL: NV2A RPC protocol/transfer smoke\n";return 1;
 }
 std::cout<<"PASS: NV2A RPC register, method, flush, VRAM/RAMIN transfer, and bounds checks\n";
 return 0;
}
