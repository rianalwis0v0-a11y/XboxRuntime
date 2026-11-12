#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>
#include "kernel_compat.h"

namespace {
constexpr uint32_t kHeaderMin=0x178, kSectionSize=56;
constexpr uint32_t kRetailEntry=0xA8FC57ABu, kDebugEntry=0x94859D4Bu;
constexpr uint32_t kReplyMagic=0x47584258;
#pragma pack(push,1)
struct Section { uint32_t flags,va,vs,raw,rawSize,name,refs,head,tail; uint8_t digest[20]; };
#pragma pack(pop)
static_assert(sizeof(Section)==kSectionSize);
struct Reply { uint32_t magic,version,status,imageBase,imageSize,entry,sectionCount; };
enum : uint32_t { OK=0, ARGUMENT=1, IO=2, FORMAT=3, LAYOUT=4, MEMORY=5 };
void* g_image=nullptr;
uint32_t U32(const std::vector<uint8_t>& b,size_t o){uint32_t v=0;std::memcpy(&v,b.data()+o,4);return v;}
bool Fits(uint64_t o,uint64_t n,uint64_t lim){return o<=lim&&n<=lim-o;}
bool VaOffset(uint32_t va,uint32_t base,uint32_t size,uint32_t& out){if(va<base)return false;uint64_t d=uint64_t(va)-base;if(d>=size)return false;out=uint32_t(d);return true;}
void Release(){if(g_image)VirtualFree(g_image,0,MEM_RELEASE);g_image=nullptr;}
uint32_t Load(const wchar_t* path,Reply& reply){
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
 if(f==INVALID_HANDLE_VALUE)return IO;
 LARGE_INTEGER fs{};
 if(!GetFileSizeEx(f,&fs)||fs.QuadPart<kHeaderMin||fs.QuadPart>0xFFFFFFFFll){CloseHandle(f);return FORMAT;}
 std::vector<uint8_t>b;try{b.resize(size_t(fs.QuadPart));}catch(...){CloseHandle(f);return MEMORY;}
 size_t pos=0;
 while(pos<b.size()){DWORD got=0,ask=DWORD(std::min<size_t>(b.size()-pos,1u<<20));if(!ReadFile(f,b.data()+pos,ask,&got,nullptr)||!got){CloseHandle(f);return IO;}pos+=got;}
 CloseHandle(f);
 if(std::memcmp(b.data(),"XBEH",4))return FORMAT;
 uint32_t base=U32(b,0x104),headers=U32(b,0x108),image=U32(b,0x10C),hs=U32(b,0x110);
 uint32_t tableVa=U32(b,0x120),count=U32(b,0x11C),encodedEntry=U32(b,0x128);
 if(!base||!image||headers<kHeaderMin||headers>b.size()||hs<kHeaderMin||hs>headers||image<headers||!count||count>96)return FORMAT;
 uint32_t table=0;if(!VaOffset(tableVa,base,image,table)||!Fits(table,uint64_t(count)*kSectionSize,headers))return FORMAT;
 std::vector<Section> sections;try{sections.resize(count);}catch(...){return MEMORY;}
 for(uint32_t i=0;i<count;i++){
  Section s{};std::memcpy(&s,b.data()+table+uint64_t(i)*kSectionSize,sizeof(s));uint32_t off=0;
  if(!s.vs||!VaOffset(s.va,base,image,off)||!Fits(off,s.vs,image)||!Fits(s.raw,s.rawSize,b.size())||s.rawSize>s.vs)return FORMAT;
  sections[i]=s;
 }
 uint32_t entry=encodedEntry^kRetailEntry;bool executable=false;
 for(const auto&s:sections)if((s.flags&4)&&entry>=s.va&&uint64_t(entry)<uint64_t(s.va)+s.vs){executable=true;break;}
 if(!executable){entry=encodedEntry^kDebugEntry;for(const auto&s:sections)if((s.flags&4)&&entry>=s.va&&uint64_t(entry)<uint64_t(s.va)+s.vs){executable=true;break;}}
 if(!executable)return FORMAT;
 void* mem=VirtualAlloc(reinterpret_cast<void*>(uintptr_t(base)),image,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!mem)return LAYOUT;
 std::memcpy(mem,b.data(),headers);
 for(const auto&s:sections){uint8_t*d=static_cast<uint8_t*>(mem)+(s.va-base);if(s.rawSize)std::memcpy(d,b.data()+s.raw,s.rawSize);if(s.vs>s.rawSize)std::memset(d+s.rawSize,0,s.vs-s.rawSize);}
 for(const auto&s:sections){
  DWORD prot=(s.flags&4)?((s.flags&1)?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ):((s.flags&1)?PAGE_READWRITE:PAGE_READONLY);
  uintptr_t p=reinterpret_cast<uintptr_t>(mem)+(s.va-base),first=p&~uintptr_t(0xFFF),last=(p+s.vs+0xFFF)&~uintptr_t(0xFFF);
  DWORD old=0;if(last>first)VirtualProtect(reinterpret_cast<void*>(first),last-first,prot,&old);
 }
 DWORD old=0;VirtualProtect(mem,headers,PAGE_READONLY,&old);
 g_image=mem;reply={kReplyMagic,1,OK,base,image,entry,count};return OK;
}
bool ReadExact(HANDLE h,void*p,DWORD n){DWORD d=0;while(d<n){DWORD got=0;if(!ReadFile(h,static_cast<char*>(p)+d,n-d,&got,nullptr)||!got)return false;d+=got;}return true;}
bool WriteExact(HANDLE h,const void*p,DWORD n){DWORD d=0;while(d<n){DWORD sent=0;if(!WriteFile(h,static_cast<const char*>(p)+d,n-d,&sent,nullptr)||!sent)return false;d+=sent;}return true;}
}
int wmain(int argc,wchar_t**argv){
 const wchar_t*pipeName=nullptr;const wchar_t*xbePath=nullptr;const wchar_t*eepromPath=nullptr;uint32_t reportVa=0;
 for(int i=1;i+1<argc;i++){if(!wcscmp(argv[i],L"--pipe"))pipeName=argv[++i];else if(!wcscmp(argv[i],L"--xbe"))xbePath=argv[++i];else if(!wcscmp(argv[i],L"--eeprom"))eepromPath=argv[++i];else if(!wcscmp(argv[i],L"--report-va"))reportVa=uint32_t(wcstoul(argv[++i],nullptr,0));}
 if(!pipeName||!xbePath||!eepromPath)return 2;
 HANDLE pipe=CreateFileW(pipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
 if(pipe==INVALID_HANDLE_VALUE)return 3;
 Reply r{};r.magic=kReplyMagic;r.version=1;r.status=Load(xbePath,r);
 HANDLE xbeThread=nullptr;
 if(r.status==OK){
  if(!XboxKernelInitializeEeprom(eepromPath))r.status=IO;
  else {
   XboxKernelSetTitlePath(xbePath);
   XboxKernelState image{r.imageBase,r.imageSize,r.entry,r.sectionCount,*reinterpret_cast<uint32_t*>(uintptr_t(r.imageBase+0x12C))};
   if(!XboxKernelResolveImports(image))r.status=FORMAT;
   else{xbeThread=CreateThread(nullptr,0,XboxKernelStartXbe,nullptr,0,nullptr);if(!xbeThread)r.status=MEMORY;}
  }
 }
 bool ok=WriteExact(pipe,&r,sizeof(r));
 uint8_t command=0;
 while(ok&&ReadExact(pipe,&command,1)){if(command==0)break;uint8_t rejected=1;if(!WriteExact(pipe,&rejected,1))break;}
 if(xbeThread){WaitForSingleObject(xbeThread,INFINITE);CloseHandle(xbeThread);}
 struct DiagnosticReport { uint32_t magic,passed,failed,skipped,firstFailedCheck; } report{};
 if(reportVa && g_image && reportVa>=r.imageBase && uint64_t(reportVa)+sizeof(report)<=uint64_t(r.imageBase)+r.imageSize){
  std::memcpy(&report,reinterpret_cast<const void*>(uintptr_t(reportVa)),sizeof(report));
  if(report.magic==0x54535258u)WriteExact(pipe,&report,sizeof(report));
 }
 XboxKernelFlushEeprom();
 Release();CloseHandle(pipe);return ok?0:4;
}
