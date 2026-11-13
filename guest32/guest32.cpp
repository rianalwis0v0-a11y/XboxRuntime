/*
 * This file contains an XboxRuntime adaptation of XBE image-loading behavior
 * from Cxbx-Reloaded's Xbe implementation.
 *
 * Upstream: https://github.com/Cxbx-Reloaded/Cxbx-Reloaded
 * Upstream files: src/common/xbe/Xbe.cpp and src/common/xbe/Xbe.h
 * Upstream copyright: Aaron Robinson (2002-2003), Patrick van Logchem (2017)
 * Upstream licence: GNU GPL version 2 or (at your option) any later version.
 *
 * XboxRuntime modifications: bounded file/VA validation, checked section,
 * certificate, library-version and TLS ranges, safe section-name reads,
 * executable entry-point validation, and guarded page protection.
 *
 * This file is distributed under GPL-2.0-or-later. See
 * third_party/cxbx-reloaded/COPYING for the licence text.
 */
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

 // Xbe::Header fields used by Cxbx-Reloaded (Xbe.h). All file and VA
 // conversions are range checked before any allocation or guest mapping.
 const uint32_t base=U32(b,0x104),headers=U32(b,0x108),image=U32(b,0x10C),headerImage=U32(b,0x110);
 const uint32_t certVa=U32(b,0x118),count=U32(b,0x11C),tableVa=U32(b,0x120);
 const uint32_t encodedEntry=U32(b,0x128),tlsVa=U32(b,0x12C),kernelThunk=U32(b,0x158);
 const uint32_t libraryCount=U32(b,0x160),libraryVa=U32(b,0x164);
 const uint64_t imageEnd=uint64_t(base)+image;
 if(!base||(base&0xFFFu)||!image||imageEnd>0x80000000ull||
    headers<kHeaderMin||headers>b.size()||headerImage<kHeaderMin||
    headerImage>headers||image<headers||!count||count>96)return FORMAT;

 // Cxbx reads the certificate, section table, library versions and TLS
 // from their image VAs. Validate each before dereferencing it.
 // Homebrew/test XBEs may intentionally omit the certificate (cert VA == 0).
 // Retail images with a certificate still receive strict bounds validation.
 if(certVa){
  uint32_t certOff=0;
  if(!VaOffset(certVa,base,image,certOff)||!Fits(certOff,0x5Cu,image)||
     !Fits(certOff,0x5Cu,b.size())||!Fits(certOff,0x5Cu,headers))return FORMAT;
  const uint32_t certSize=U32(b,certOff);
  if(certSize<0x5Cu||!Fits(certOff,certSize,b.size())||!Fits(certOff,certSize,headers))return FORMAT;
 }

 uint32_t table=0;
 if(!VaOffset(tableVa,base,image,table)||!Fits(table,uint64_t(count)*kSectionSize,headers))return FORMAT;
 if(libraryCount>1024u)return FORMAT;
 uint32_t libraryOff=0;
 if(libraryCount&&(!libraryVa||!VaOffset(libraryVa,base,image,libraryOff)||
    !Fits(libraryOff,uint64_t(libraryCount)*16u,image)))return FORMAT;
 uint32_t tlsOff=0;
 if(tlsVa&&(!VaOffset(tlsVa,base,image,tlsOff)||!Fits(tlsOff,24u,image)))return FORMAT;

 std::vector<Section> sections;
 try{sections.resize(count);}catch(...){return MEMORY;}
 for(uint32_t i=0;i<count;i++){
  Section s{};std::memcpy(&s,b.data()+table+uint64_t(i)*kSectionSize,sizeof(s));
  uint32_t sectionOff=0;
  if(!s.vs||!VaOffset(s.va,base,image,sectionOff)||
     !Fits(sectionOff,s.vs,image)||!Fits(s.raw,s.rawSize,b.size())||
     s.rawSize>s.vs)return FORMAT;
  sections[i]=s;
 }

 // Decode the retail/debug entry point and require it to land in executable
 // section memory. Chihiro's separate key is intentionally not guessed here.
 const uint32_t entryKey=(kernelThunk&0x80000000u)?kDebugEntry:kRetailEntry;
 const uint32_t entry=encodedEntry^entryKey;bool executable=false;
 for(const auto&s:sections)if((s.flags&4u)&&entry>=s.va&&uint64_t(entry)<uint64_t(s.va)+s.vs){executable=true;break;}
 if(!executable)return FORMAT;

 // XBE virtual addresses are absolute; the preferred image base is required.
 void* mem=VirtualAlloc(reinterpret_cast<void*>(uintptr_t(base)),image,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!mem)return LAYOUT;
 std::memcpy(mem,b.data(),headers);
 for(const auto&s:sections){
  uint8_t*d=static_cast<uint8_t*>(mem)+(s.va-base);
  if(s.rawSize)std::memcpy(d,b.data()+s.raw,s.rawSize);
  if(s.vs>s.rawSize)std::memset(d+s.rawSize,0,s.vs-s.rawSize);
 }

 // Resolve each section name from its mapped VA as Cxbx does, but cap the
 // scan to the XBE's mapped image so malformed names cannot read past it.
 for(const auto&s:sections){
  if(!s.name)continue; // Cxbx permits sections with an absent name pointer.
  uint32_t nameOff=0;
  if(!VaOffset(s.name,base,image,nameOff)||!Fits(nameOff,1,image)){VirtualFree(mem,0,MEM_RELEASE);return FORMAT;}
  const uint8_t*name=static_cast<const uint8_t*>(mem)+nameOff;
  size_t available=std::min<size_t>(9u,size_t(image-nameOff));
  for(size_t n=0;n<available;n++)if(name[n]==0)break;
 }

 // Apply Xbox section permissions with page-rounded ranges constrained to
 // the reserved image. Header protection is applied after section mapping.
 const uint64_t imageBegin=base,imageLimit=imageEnd;
 for(const auto&s:sections){
  DWORD prot=(s.flags&4u)?((s.flags&1u)?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ):
                         ((s.flags&1u)?PAGE_READWRITE:PAGE_READONLY);
  uint64_t p=uint64_t(base)+(s.va-base),first=p&~uint64_t(0xFFFu);
  uint64_t last=(p+s.vs+0xFFFu)&~uint64_t(0xFFFu);
  if(first<imageBegin)first=imageBegin;
  if(last>imageLimit)last=imageLimit;
  if(last>first){DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(uintptr_t(first)),SIZE_T(last-first),prot,&old)){VirtualFree(mem,0,MEM_RELEASE);return MEMORY;}}
 }
 DWORD old=0;
 if(!VirtualProtect(mem,headers,PAGE_READONLY,&old)){VirtualFree(mem,0,MEM_RELEASE);return MEMORY;}
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
  }
 }
 bool ok=WriteExact(pipe,&r,sizeof(r));
 uint8_t command=0;bool released=false;
 while(ok&&ReadExact(pipe,&command,1)){
  if(command==0){released=true;break;}
  uint8_t rejected=1;if(!WriteExact(pipe,&rejected,1))break;
 }
 /* Do not execute guest code until the controller has received the launch
  * reply and explicitly releases the XBE. */
 if(ok&&released&&r.status==OK){
  xbeThread=CreateThread(nullptr,0,XboxKernelStartXbe,nullptr,0,nullptr);
  if(!xbeThread)ok=false;
 }
 if(xbeThread){WaitForSingleObject(xbeThread,INFINITE);CloseHandle(xbeThread);}
 struct DiagnosticReport { uint32_t magic,passed,failed,skipped,firstFailedCheck,progress,directoryOpenStatus,directoryUnicodeValue; } report{};
 if(reportVa && g_image && reportVa>=r.imageBase && uint64_t(reportVa)+sizeof(report)<=uint64_t(r.imageBase)+r.imageSize){
  std::memcpy(&report,reinterpret_cast<const void*>(uintptr_t(reportVa)),sizeof(report));
  if(report.magic==0x54535258u)WriteExact(pipe,&report,sizeof(report));
 }
 XboxKernelFlushEeprom();
 Release();CloseHandle(pipe);return ok?0:4;
}
