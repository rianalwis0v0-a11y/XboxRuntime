#include "guest_api_registry.h"
#include "xnet_compat.h"
#include <cstring>
#include <cstdint>
#include <iterator>
#include <cwchar>
#include <cwctype>
#include <string>
namespace {
template<class F> std::uint32_t GuestAddress(F f) noexcept {
 static_assert(sizeof(void*)==4,"guest API addresses require x86");
 return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(f));
}
struct Entry { const char* name; std::uint32_t address; };
#define GUEST_API(s) {#s,GuestAddress(&s)}
const Entry kApis[]={
 GUEST_API(XNetStartup),GUEST_API(XNetCleanup),GUEST_API(XNetDnsLookup),GUEST_API(XNetDnsRelease),
 GUEST_API(XNetInAddrToString),GUEST_API(XNetServerToInAddr),GUEST_API(XNetInAddrToXnAddr),
 GUEST_API(XNetGetEthernetLinkStatus),GUEST_API(XNetGetTitleXnAddr),
 GUEST_API(XSocketCreate),GUEST_API(XSocketClose),GUEST_API(XSocketBind),GUEST_API(XSocketConnect),
 GUEST_API(XSocketListen),GUEST_API(XSocketAccept),GUEST_API(XSocketShutdown),GUEST_API(XSocketIOCTLSocket),
 GUEST_API(XSocketSetSockOpt),GUEST_API(XSocketGetSockOpt),GUEST_API(XSocketGetSockName),
 GUEST_API(XSocketGetPeerName),GUEST_API(XSocketSend),GUEST_API(XSocketRecv),GUEST_API(XSocketSendTo),
 GUEST_API(XSocketRecvFrom),GUEST_API(XSocketHTONL),GUEST_API(XSocketNTOHL),GUEST_API(XSocketHTONS),
 GUEST_API(XSocketNTOHS)
};
#undef GUEST_API
bool Range(std::uint32_t b,std::uint32_t s,std::uint32_t a,std::uint32_t n) noexcept {
 return a>=b && std::uint64_t(a)+n<=std::uint64_t(b)+s;
}
bool WriteGuestThunk(std::uint32_t address,std::uint32_t target) noexcept {
 auto* destination=reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
 DWORD previous=0;
 if(!VirtualProtect(destination,sizeof(target),PAGE_EXECUTE_READWRITE,&previous))return false;
 std::memcpy(destination,&target,sizeof(target));
 DWORD ignored=0;
 return VirtualProtect(destination,sizeof(target),previous,&ignored)!=FALSE;
}
bool IsXNet(std::uint32_t b,std::uint32_t s,std::uint32_t a) noexcept {
 if(!Range(b,s,a,2)) return false;
 const auto* p=reinterpret_cast<const wchar_t*>(static_cast<std::uintptr_t>(a));
 wchar_t name[32]{}; std::size_t i=0;
 for(;i+1<std::size(name);++i){
  const std::uint64_t at=std::uint64_t(a)+i*sizeof(wchar_t);
  if(at>0xFFFFFFFFull||!Range(b,s,static_cast<std::uint32_t>(at),sizeof(wchar_t))) return false;
  const wchar_t c=p[i]; if(!c) break; if(c>0x7F) return false;
  name[i]=static_cast<wchar_t>(std::towupper(c));
 }
 if(i+1==std::size(name)||p[i]!=L'\0') return false;
 const std::wstring value(name); return value==L"XNET"||value==L"XNET.DLL";
}
}
extern "C" std::uint32_t __cdecl XboxGuestApiLookup(const char* n){
 if(!n||!*n)return 0; for(const auto& e:kApis)if(std::strcmp(n,e.name)==0)return e.address; return 0;
}
extern "C" std::uint32_t __cdecl XboxGuestApiCount(){return static_cast<std::uint32_t>(std::size(kApis));}
extern "C" bool __cdecl XboxGuestResolveNonKernelImports(std::uint32_t base,std::uint32_t size,std::uint32_t dir){
 if(!base||!size||std::uint64_t(base)+size>0x100000000ull)return false;
 if(!dir)return true;
 struct Descriptor{std::uint32_t firstThunk,wideCharName;};
 static_assert(sizeof(Descriptor)==8,"XBE import descriptor size");
 bool ended=false;
 for(std::uint32_t di=0;di<64;++di){
  const std::uint64_t da=std::uint64_t(dir)+std::uint64_t(di)*sizeof(Descriptor);
  if(da>0xFFFFFFFFull)return false; const auto daddr=static_cast<std::uint32_t>(da);
  if(!Range(base,size,daddr,sizeof(Descriptor)))return false;
  Descriptor d{};std::memcpy(&d,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(daddr)),sizeof(d));
  if(!d.firstThunk){ended=d.wideCharName==0;break;}
  if(!d.wideCharName||!IsXNet(base,size,d.wideCharName)||!Range(base,size,d.firstThunk,4))return false;
  bool thunkEnded=false;
  for(std::uint32_t ti=0;ti<4096;++ti){
   const std::uint64_t ta=std::uint64_t(d.firstThunk)+std::uint64_t(ti)*4;
   if(ta>0xFFFFFFFFull)return false;const auto taddr=static_cast<std::uint32_t>(ta);
   if(!Range(base,size,taddr,4))return false;
   std::uint32_t nameVa=0;std::memcpy(&nameVa,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(taddr)),4);
   if(!nameVa){thunkEnded=true;break;}
   if(nameVa&0x80000000u)return false; // No guessed ordinal table.
   if(!Range(base,size,nameVa,3)){
    if(nameVa>=size||std::uint64_t(base)+nameVa>0xFFFFFFFFull)return false;
    nameVa+=base;
   }
   if(!Range(base,size,nameVa,3))return false;
   const char* name=reinterpret_cast<const char*>(static_cast<std::uintptr_t>(nameVa+2));
   const std::uint64_t remaining=std::uint64_t(base)+size-(std::uint64_t(nameVa)+2);
   if(remaining>0xFFFFFFFFull||!std::memchr(name,0,static_cast<std::size_t>(remaining)))return false;
   const std::uint32_t target=XboxGuestApiLookup(name);if(!target)return false;
   // Import tables commonly live in read-only sections; restore page protection after patching.
   if(!WriteGuestThunk(taddr,target))return false;
  }
  if(!thunkEnded)return false;
 }
 return ended;
}
