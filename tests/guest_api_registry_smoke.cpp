#include "guest_api_registry.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#pragma pack(push,1)
struct Descriptor{std::uint32_t firstThunk,wideCharName;};
#pragma pack(pop)
bool Fixture(bool ordinal,bool knownName,bool knownLibrary){
 alignas(4) std::uint8_t image[2048]{};auto b=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image));
 Descriptor d{b+0x200,b+0x300};std::memcpy(image+0x100,&d,sizeof(d));
 const wchar_t* lib=knownLibrary?L"XNET":L"NOTXNET";std::memcpy(image+0x300,lib,(std::wcslen(lib)+1)*sizeof(wchar_t));
 std::uint32_t thunk=ordinal?0x80000001u:b+0x400;std::memcpy(image+0x200,&thunk,4);
 std::uint16_t hint=0;std::memcpy(image+0x400,&hint,2);const char* fn=knownName?"XNetStartup":"NotAnXNetFunction";
 std::memcpy(image+0x402,fn,std::strlen(fn)+1);
 return XboxGuestResolveNonKernelImports(b,sizeof(image),b+0x100);
}
int main(){
 if(!XboxGuestApiLookup("XNetStartup")||XboxGuestApiLookup("NotAnApi")||XboxGuestApiCount()!=29)return 1;
 if(!Fixture(false,true,true))return 2;
 if(Fixture(true,true,true))return 3;
 if(Fixture(false,false,true))return 4;
 if(Fixture(false,true,false))return 5;
 std::puts("guest_api_registry_smoke: named XNET import binding and rejection paths passed");return 0;
}
