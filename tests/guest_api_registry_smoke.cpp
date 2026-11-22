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
 const char* required[]={
  "XNetStartup","XNetCleanup","XNetCreateKey","XNetRegisterKey","XNetUnregisterKey",
  "XNetDnsLookup","XNetDnsRelease","XNetRandom","XNetInAddrToString",
  "XNetXnAddrToInAddr","XNetServerToInAddr","XNetInAddrToXnAddr",
  "XNetUnregisterInAddr","XNetGetEthernetLinkStatus","XNetGetTitleXnAddr","XNetGetDebugXnAddr",
  "XSocketCreate","XSocketClose","XSocketBind","XSocketConnect","XSocketListen",
  "XSocketAccept","XSocketShutdown","XSocketIOCTLSocket","XSocketSelect",
  "XSocketSetSockOpt","XSocketGetSockOpt","XSocketGetSockName","XSocketGetPeerName",
  "XSocketSend","XSocketRecv","XSocketSendTo","XSocketRecvFrom","XSocketHTONL",
  "XSocketNTOHL","XSocketHTONS","XSocketNTOHS",
  "WSAStartup","WSACleanup","WSACreateEvent","WSACloseEvent","WSASetEvent",
  "WSAResetEvent","WSAWaitForMultipleEvents","WSAGetLastError","WSASetLastError","inet_addr"
 };
 if(XboxGuestApiCount()!=sizeof(required)/sizeof(required[0]))return 1;
 for(const char* name:required)if(!XboxGuestApiLookup(name)){std::printf("missing XNET guest API: %s\n",name);return 2;}
 if(XboxGuestApiLookup("NotAnApi"))return 3;
 if(!Fixture(false,true,true))return 4;
 if(Fixture(true,true,true))return 5;
 if(Fixture(false,false,true))return 6;
 if(Fixture(false,true,false))return 7;
 std::puts("guest_api_registry_smoke: all implemented XNet/XSocket/WSA imports bind; rejection paths passed");
 return 0;
}
