#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <cwchar>
#include <vector>

namespace {
constexpr uint32_t kReplyMagic=0x47584258;
enum : int { XBE_OK=0, XBE_E_ARGUMENT=1, XBE_E_START=7, XBE_E_PIPE=8,
             XBE_E_GUEST=9, XBE_E_GUEST_STARTED=10 };
#pragma pack(push,1)
struct Reply { uint32_t magic,version,status,imageBase,imageSize,entry,sectionCount; };
#pragma pack(pop)
struct Guest {
 HANDLE process=nullptr,thread=nullptr,pipe=nullptr;
 uint32_t base=0,size=0,entry=0,sections=0;
} g;
bool started=false;
bool releaseSent=false;
HANDLE rpcThread=nullptr;
#pragma pack(push,1)
struct GpuRpcRequest { uint32_t magic,version,opcode,payloadSize,requestId,arg0,arg1,arg2,arg3; };
struct GpuRpcResponse { uint32_t magic,version,requestId,status; uint64_t value; };
#pragma pack(pop)
constexpr uint32_t kGpuRpcMagic=0x43525047u; // GPRC
bool ExactRead(HANDLE h,void*p,DWORD n);
bool ExactWrite(HANDLE h,const void*p,DWORD n);
DWORD WINAPI ServeGpuRpc(void*) {
 HMODULE gpu=GetModuleHandleW(L"nv2a.dll");
 using ReadFn=uint64_t (__cdecl*)(uint32_t,unsigned int);
 using WriteFn=int (__cdecl*)(uint32_t,uint64_t,unsigned int);
 using SubmitFn=int (__cdecl*)(uint32_t,uint32_t,uint32_t);
 using FlushFn=void (__cdecl*)();
 auto read=reinterpret_cast<ReadFn>(gpu?GetProcAddress(gpu,"XboxNV2A_ReadRegister"):nullptr);
 auto write=reinterpret_cast<WriteFn>(gpu?GetProcAddress(gpu,"XboxNV2A_WriteRegister"):nullptr);
 auto submit=reinterpret_cast<SubmitFn>(gpu?GetProcAddress(gpu,"XboxNV2A_SubmitMethod"):nullptr);
 auto flush=reinterpret_cast<FlushFn>(gpu?GetProcAddress(gpu,"XboxNV2A_Flush"):nullptr);
 while(g.pipe&&g.pipe!=INVALID_HANDLE_VALUE){
  GpuRpcRequest q{}; if(!ExactRead(g.pipe,&q,sizeof(q)))break;
  GpuRpcResponse a{kGpuRpcMagic,1,q.requestId,1,0};
  if(q.magic==kGpuRpcMagic&&q.version==1&&q.payloadSize==0){
   switch(q.opcode){
   case 1: if(read&&q.arg1<=4&&(q.arg1==1||q.arg1==2||q.arg1==4)){a.value=read(q.arg0,q.arg1);a.status=0;}break;
   case 2: if(write&&(q.arg3==1||q.arg3==2||q.arg3==4)){uint64_t v=(uint64_t(q.arg2)<<32)|q.arg1;a.status=write(q.arg0,v,q.arg3)?0u:1u;}break;
   case 3: if(submit)a.status=submit(q.arg0,q.arg1,q.arg2)?0u:1u;break;
   case 4: if(flush){flush();a.status=0;}break;
   default: break;
   }
  }
  if(!ExactWrite(g.pipe,&a,sizeof(a)))break;
 }
 return 0;
}
std::wstring Quote(const std::wstring&s){std::wstring q=L"\"";for(wchar_t c:s){if(c==L'\"')q+=L'\\';q+=c;}q+=L'\"';return q;}
bool ExactRead(HANDLE h,void*p,DWORD n){DWORD d=0;while(d<n){DWORD got=0;if(!ReadFile(h,static_cast<char*>(p)+d,n-d,&got,nullptr)||!got)return false;d+=got;}return true;}
bool ExactWrite(HANDLE h,const void*p,DWORD n){DWORD d=0;while(d<n){DWORD sent=0;if(!WriteFile(h,static_cast<const char*>(p)+d,n-d,&sent,nullptr)||!sent)return false;d+=sent;}return true;}
void StopGuest(){
 /* Zero is the explicit XBE launch-release command. Never send it while
  * unloading a guest that has not been approved for execution. */
 if(g.pipe&&g.pipe!=INVALID_HANDLE_VALUE){
  if(!releaseSent){uint8_t abortCommand=1;ExactWrite(g.pipe,&abortCommand,1);}
  CloseHandle(g.pipe);
 }
 if(rpcThread){WaitForSingleObject(rpcThread,1500);CloseHandle(rpcThread);rpcThread=nullptr;}
 if(g.process){if(WaitForSingleObject(g.process,1500)==WAIT_TIMEOUT)TerminateProcess(g.process,0);WaitForSingleObject(g.process,1000);}
 if(g.thread)CloseHandle(g.thread);if(g.process)CloseHandle(g.process);g={};releaseSent=false;
}
std::wstring EepromPath() {
 wchar_t windowsPath[32768]{};
 UINT n=GetWindowsDirectoryW(windowsPath,UINT(_countof(windowsPath)));
 if(!n||n>=_countof(windowsPath))return {};
 std::wstring win(windowsPath,n);size_t slash=win.find_last_of(L"\\\\");
 if(slash==std::wstring::npos)return {};
 std::wstring root=win.substr(0,slash);
 std::wstring runtime=root+L"\\\\XboxRuntime";
 std::wstring system=runtime+L"\\\\system";
 if(!CreateDirectoryW(runtime.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return {};
 if(!CreateDirectoryW(system.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return {};
 return system+L"\\\\eeprom.bin";
}
std::wstring ModuleDirectory(){
 HMODULE self=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
   reinterpret_cast<LPCWSTR>(&ModuleDirectory),&self))return {};
 std::vector<wchar_t>b(32768);DWORD n=GetModuleFileNameW(self,b.data(),DWORD(b.size()));
 if(!n||n>=b.size())return {};std::wstring p(b.data(),n);size_t slash=p.find_last_of(L"\\/");if(slash==std::wstring::npos)return {};return p.substr(0,slash);
}
int StartGuest(const wchar_t*xbe){
 if(!xbe||!*xbe)return XBE_E_ARGUMENT;StopGuest();
 std::wstring dir=ModuleDirectory();if(dir.empty())return XBE_E_START;
 std::wstring exe=dir+L"\\xbe_guest32.exe";
 if(GetFileAttributesW(exe.c_str())==INVALID_FILE_ATTRIBUTES)return XBE_E_START;
 wchar_t pipeName[128]{};swprintf_s(pipeName,L"\\\\.\\pipe\\XboxRuntimeGuest-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
 HANDLE pipe=CreateNamedPipeW(pipeName,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT,1,256,256,0,nullptr);
 if(pipe==INVALID_HANDLE_VALUE)return XBE_E_PIPE;
 std::wstring eeprom=EepromPath();if(eeprom.empty()){CloseHandle(pipe);return XBE_E_START;}
 std::wstring cmd=Quote(exe)+L" --pipe "+Quote(pipeName)+L" --xbe "+Quote(xbe)+L" --eeprom "+Quote(eeprom);
 STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi)){CloseHandle(pipe);return XBE_E_START;}
 g.process=pi.hProcess;g.thread=pi.hThread;g.pipe=pipe;
 bool connected=false;
 for(unsigned i=0;i<1500;i++){
  if(ConnectNamedPipe(pipe,nullptr)){connected=true;break;}
  DWORD e=GetLastError();if(e==ERROR_PIPE_CONNECTED){connected=true;break;}
  if(e!=ERROR_PIPE_LISTENING&&e!=ERROR_NO_DATA)break;
  if(WaitForSingleObject(g.process,0)==WAIT_OBJECT_0)break;
  Sleep(10);
 }
 if(!connected){StopGuest();return XBE_E_PIPE;}
 DWORD mode=PIPE_READMODE_BYTE|PIPE_WAIT;if(!SetNamedPipeHandleState(pipe,&mode,nullptr,nullptr)){StopGuest();return XBE_E_PIPE;}
 Reply r{};if(!ExactRead(pipe,&r,sizeof(r))||r.magic!=kReplyMagic||r.version!=1){StopGuest();return XBE_E_PIPE;}
 if(r.status!=0){StopGuest();return XBE_E_GUEST;}
 g.base=r.imageBase;g.size=r.imageSize;g.entry=r.entry;g.sections=r.sectionCount;
 /* The guest waits after sending its validated launch reply. Release only
  * after the host accepts it, so failed launches never execute guest code. */
 const uint8_t releaseCommand=0;
 if(!ExactWrite(pipe,&releaseCommand,1)){StopGuest();return XBE_E_PIPE;}
 releaseSent=true;
 rpcThread=CreateThread(nullptr,0,ServeGpuRpc,nullptr,0,nullptr);
 if(!rpcThread){StopGuest();return XBE_E_PIPE;}
 OutputDebugStringW(L"[XbeLoader] XBE mapped and explicitly released in dedicated native x86 guest process.\\n");
 return XBE_E_GUEST_STARTED;
}
}
extern "C" __declspec(dllexport) int __cdecl ModuleStart(){started=true;return XBE_OK;}
extern "C" __declspec(dllexport) void __cdecl ModuleStop(){StopGuest();started=false;}
extern "C" __declspec(dllexport) int __cdecl XbeLoadImage(const wchar_t*path){if(!started)return XBE_E_ARGUMENT;return StartGuest(path);}
extern "C" __declspec(dllexport) void __cdecl XbeUnloadImage(){StopGuest();}
extern "C" __declspec(dllexport) int __cdecl XbeGetImageInfo(uint32_t*base,uint32_t*size,uint32_t*entry,uint32_t*count){
 if(!g.process)return XBE_E_ARGUMENT;if(base)*base=g.base;if(size)*size=g.size;if(entry)*entry=g.entry;if(count)*count=g.sections;return XBE_OK;
}
extern "C" __declspec(dllexport) int __cdecl LoadXbe(const wchar_t*path){int r=XbeLoadImage(path);return r==XBE_E_GUEST_STARTED?XBE_OK:r;}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
