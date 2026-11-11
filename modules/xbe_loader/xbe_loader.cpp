#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace {
constexpr uint32_t kHeaderMin=0x178, kSectionSize=56, kEntryXor=0xA8FC57ABu;
enum : int { XBE_OK=0, XBE_E_ARGUMENT=1, XBE_E_IO=2, XBE_E_FORMAT=3,
             XBE_E_LAYOUT=4, XBE_E_MEMORY=5, XBE_E_NO_CPU=6 };
#pragma pack(push,1)
struct Section {
 uint32_t flags, virtualAddress, virtualSize, rawAddress, rawSize, nameAddress,
          referenceCount, headSharedPageRefCountAddress, tailSharedPageRefCountAddress;
 uint8_t digest[20];
};
#pragma pack(pop)
static_assert(sizeof(Section)==kSectionSize);
struct State { void* base=nullptr; uint32_t imageBase=0,imageSize=0,entry=0,count=0; std::wstring path; } g;
bool started=false;

uint32_t U32(const std::vector<uint8_t>& b,size_t o) { uint32_t v=0; std::memcpy(&v,b.data()+o,4); return v; }
bool Fits(uint64_t o,uint64_t n,uint64_t limit) { return o<=limit && n<=limit-o; }
bool VaOffset(uint32_t va,uint32_t base,uint32_t size,uint32_t& out) {
 if(va<base) return false; uint64_t d=uint64_t(va)-base; if(d>=size) return false; out=uint32_t(d); return true;
}
DWORD Protection(uint32_t f) {
 bool x=(f&4)!=0,w=(f&1)!=0;
 return x?(w?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ):(w?PAGE_READWRITE:PAGE_READONLY);
}
void Unmap() { if(g.base) VirtualFree(g.base,0,MEM_RELEASE); g={}; }

int MapFile(const wchar_t* path) {
 if(!path||!*path) return XBE_E_ARGUMENT;
 HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
 if(h==INVALID_HANDLE_VALUE) return XBE_E_IO;
 LARGE_INTEGER fs{};
 if(!GetFileSizeEx(h,&fs)||fs.QuadPart<kHeaderMin||fs.QuadPart>0xFFFFFFFFll) { CloseHandle(h); return XBE_E_FORMAT; }
 std::vector<uint8_t> b;
 try { b.resize(size_t(fs.QuadPart)); } catch(...) { CloseHandle(h); return XBE_E_MEMORY; }
 size_t done=0;
 while(done<b.size()) { DWORD got=0,ask=DWORD(std::min<size_t>(b.size()-done,1u<<20));
  if(!ReadFile(h,b.data()+done,ask,&got,nullptr)||!got) { CloseHandle(h); return XBE_E_IO; } done+=got; }
 CloseHandle(h);
 if(std::memcmp(b.data(),"XBEH",4)!=0) return XBE_E_FORMAT;

 const uint32_t base=U32(b,0x104), headers=U32(b,0x108), image=U32(b,0x10C),
  headerStruct=U32(b,0x110), tableVa=U32(b,0x120), count=U32(b,0x11C),
  entry=U32(b,0x128)^kEntryXor;
 if(!base||!image||headers<kHeaderMin||headers>b.size()||headerStruct<kHeaderMin||
    headerStruct>headers||image<headers||!count||count>96) return XBE_E_FORMAT;
 uint32_t table=0;
 if(!VaOffset(tableVa,base,image,table)||
    !Fits(table,uint64_t(count)*kSectionSize,headers)) return XBE_E_FORMAT;

 std::vector<Section> sections;
 try { sections.resize(count); } catch(...) { return XBE_E_MEMORY; }
 bool entryExec=false;
 for(uint32_t i=0;i<count;i++) {
  Section s{}; std::memcpy(&s,b.data()+table+uint64_t(i)*kSectionSize,sizeof(s));
  uint32_t vo=0;
  if(!s.virtualSize||!VaOffset(s.virtualAddress,base,image,vo)||
     !Fits(vo,s.virtualSize,image)||!Fits(s.rawAddress,s.rawSize,b.size())||
     s.rawSize>s.virtualSize) return XBE_E_FORMAT;
  uint64_t end=uint64_t(s.virtualAddress)+s.virtualSize;
  if((s.flags&4)&&entry>=s.virtualAddress&&uint64_t(entry)<end) entryExec=true;
  sections[i]=s;
 }
 if(!entryExec) return XBE_E_FORMAT;

 // Guest addresses remain 32-bit values. Reserve the declared image base
 // explicitly; never cast an XBE entry point into a native function pointer.
 void* mapped=VirtualAlloc(reinterpret_cast<void*>(uintptr_t(base)),image,
                           MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!mapped) return XBE_E_LAYOUT;
 std::memcpy(mapped,b.data(),headers);
 for(const Section& s:sections) {
  uint32_t off=s.virtualAddress-base; uint8_t* dst=static_cast<uint8_t*>(mapped)+off;
  if(s.rawSize) std::memcpy(dst,b.data()+s.rawAddress,s.rawSize);
  if(s.virtualSize>s.rawSize) std::memset(dst+s.rawSize,0,s.virtualSize-s.rawSize);
 }
 for(const Section& s:sections) {
  constexpr uintptr_t page=0x1000;
  uintptr_t begin=reinterpret_cast<uintptr_t>(mapped)+(s.virtualAddress-base);
  uintptr_t first=begin&~(page-1), last=(begin+s.virtualSize+page-1)&~(page-1);
  DWORD old=0; VirtualProtect(reinterpret_cast<void*>(first),last-first,Protection(s.flags),&old);
 }
 DWORD old=0; VirtualProtect(mapped,headers,PAGE_READONLY,&old);
 Unmap();
 g.base=mapped; g.imageBase=base; g.imageSize=image; g.entry=entry; g.count=count; g.path=path;
 OutputDebugStringW(L"[XbeLoader] XBE validated and mapped; guest CPU is not connected.\n");
 return XBE_OK;
}
}

extern "C" __declspec(dllexport) int __cdecl ModuleStart() {
 started=true; OutputDebugStringW(L"[XbeLoader] Module ready.\n"); return XBE_OK;
}
extern "C" __declspec(dllexport) void __cdecl ModuleStop() { Unmap(); started=false; }
extern "C" __declspec(dllexport) int __cdecl XbeLoadImage(const wchar_t* path) {
 if(!started) return XBE_E_ARGUMENT; Unmap(); return MapFile(path);
}
extern "C" __declspec(dllexport) void __cdecl XbeUnloadImage() { Unmap(); }
extern "C" __declspec(dllexport) int __cdecl XbeGetImageInfo(
 uint32_t* base,uint32_t* size,uint32_t* entry,uint32_t* count) {
 if(!g.base) return XBE_E_ARGUMENT;
 if(base)*base=g.imageBase; if(size)*size=g.imageSize; if(entry)*entry=g.entry; if(count)*count=g.count;
 return XBE_OK;
}
// main.dll's launch ABI: 0 means launched. Mapping alone is not a launch;
// return an explicit status until the x86 CPU + Xbox kernel are integrated.
extern "C" __declspec(dllexport) int __cdecl LoadXbe(const wchar_t* path) {
 int r=XbeLoadImage(path); return r==XBE_OK?XBE_E_NO_CPU:r;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID) { return TRUE; }
