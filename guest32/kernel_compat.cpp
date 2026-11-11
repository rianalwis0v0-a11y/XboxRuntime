#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <intrin.h>
#include <cstring>
#include <cwchar>
#include <cctype>
#include <cwctype>
#include <string>
#include "kernel_compat.h"

extern "C" __declspec(naked) void KernelDispatch();
namespace {
constexpr uint32_t kRetailThunk=0x5B6D40B6u, kDebugThunk=0xEFB1F152u;
constexpr uint32_t kThunkMax=379;
static uint8_t* g_image=nullptr;
static uint32_t g_base=0,g_size=0,g_entry=0,g_tls=0;
static void* g_stubPage=nullptr;
static uint32_t g_stubCount=0;
static void* g_savedAv=nullptr;
static uint32_t g_data[379]{};
static std::wstring g_titleDirectory;

bool InImage(uint32_t va,uint32_t n=4){return va>=g_base&&uint64_t(va)+n<=uint64_t(g_base)+g_size;}
void* StubFor(uint32_t ordinal){
    if(!g_stubPage||g_stubCount>=kThunkMax)return nullptr;
    uint8_t* p=static_cast<uint8_t*>(g_stubPage)+g_stubCount*10;
    p[0]=0x68;std::memcpy(p+1,&ordinal,4);p[5]=0xE9;
    int32_t rel=int32_t(reinterpret_cast<uintptr_t>(&KernelDispatch)-reinterpret_cast<uintptr_t>(p+10));
    std::memcpy(p+6,&rel,4);++g_stubCount;return p;
}
void* DataFor(uint32_t ordinal){
    switch(ordinal){
    case 16: case 22: case 30: case 31: case 88: case 89:
    case 102: case 120: case 154: case 156: case 157: case 162:
    case 164: case 321: case 322: case 323: case 324: case 325:
    case 326: case 353: case 354: case 355: case 356: case 357:
        return &g_data[ordinal];
    default:return nullptr;
    }
}
extern "C" void* __stdcall K_ExAllocatePool(uint32_t n){return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n?n:1);}
extern "C" void* __stdcall K_ExAllocatePoolWithTag(uint32_t n,uint32_t){return K_ExAllocatePool(n);}
extern "C" void __stdcall K_ExFreePool(void*p){if(p)HeapFree(GetProcessHeap(),0,p);}
extern "C" uint32_t __stdcall K_ExQueryPoolBlockSize(void*p){return p?HeapSize(GetProcessHeap(),0,p):0xFFFFFFFFu;}
extern "C" void* __stdcall K_AvGetSavedDataAddress(){return g_savedAv;}
extern "C" void __stdcall K_AvSetSavedDataAddress(void*p){g_savedAv=p;}
extern "C" LONG __fastcall K_InterlockedCompareExchange(volatile LONG*p,LONG e,LONG c){return InterlockedCompareExchange(p,e,c);}
extern "C" LONG __fastcall K_InterlockedDecrement(volatile LONG*p){return InterlockedDecrement(p);}
extern "C" LONG __fastcall K_InterlockedIncrement(volatile LONG*p){return InterlockedIncrement(p);}
extern "C" LONG __fastcall K_InterlockedExchange(volatile LONG*p,LONG v){return InterlockedExchange(p,v);}
extern "C" LONG __fastcall K_InterlockedExchangeAdd(volatile LONG*p,LONG v){return InterlockedExchangeAdd(p,v);}
extern "C" DWORD __stdcall K_KeGetCurrentIrql(){return 0;}
extern "C" HANDLE __stdcall K_KeGetCurrentThread(){return GetCurrentThread();}
extern "C" LONG __stdcall K_KeDelayExecutionThread(uint32_t,BOOL, const LARGE_INTEGER* interval){
    if(!interval){Sleep(0);return 0;}
    LONGLONG ticks=interval->QuadPart;
    if(ticks<0){ULONGLONG ms=ULONGLONG(-ticks)/10000ull;Sleep(DWORD(ms>MAXDWORD?MAXDWORD:ms));}
    else Sleep(0);
    return 0;
}
extern "C" uint64_t __stdcall K_KeQueryPerformanceCounter(uint64_t*frequency){
    LARGE_INTEGER c{},f{};QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);if(frequency)*frequency=f.QuadPart;return c.QuadPart;
}
extern "C" uint64_t __stdcall K_KeQueryPerformanceFrequency(){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}
extern "C" void __stdcall K_KeQuerySystemTime(LARGE_INTEGER*out){if(out){FILETIME ft{};GetSystemTimeAsFileTime(&ft);out->LowPart=ft.dwLowDateTime;out->HighPart=LONG(ft.dwHighDateTime);}}
extern "C" void* __stdcall K_MmAllocateContiguousMemory(uint32_t n){return VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
extern "C" void* __stdcall K_MmAllocateContiguousMemoryEx(uint32_t n,uint32_t,uint32_t,uint32_t,uint32_t){return K_MmAllocateContiguousMemory(n);}
extern "C" void __stdcall K_MmFreeContiguousMemory(void*p){if(p)VirtualFree(p,0,MEM_RELEASE);}
extern "C" void* __stdcall K_MmGetPhysicalAddress(void*p){return p;}
extern "C" BOOL __stdcall K_MmIsAddressValid(void*p){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT;}
struct GuestUnicodeString { USHORT Length,MaximumLength; wchar_t* Buffer; };
struct GuestObjectAttributes { ULONG Length; HANDLE RootDirectory; GuestUnicodeString* ObjectName; ULONG Attributes; void* SecurityDescriptor; void* SecurityQualityOfService; };
struct GuestIoStatusBlock { union { LONG Status; void* Pointer; }; ULONG Information; };
std::wstring TranslateXboxPath(const GuestUnicodeString* name){
    if(!name||!name->Buffer||!name->Length)return {};
    std::wstring p(name->Buffer,name->Length/sizeof(wchar_t));
    const std::wstring cd=L"\\Device\\Cdrom0";
    const std::wstring hd=L"\\Device\\Harddisk0\\Partition1";
    if(p.rfind(cd,0)==0)p=g_titleDirectory+p.substr(cd.size());
    else if(p.rfind(hd,0)==0)p=g_titleDirectory+p.substr(hd.size());
    else if(p.rfind(L"\\??\\",0)==0)p=p.substr(4);
    else if(p.size()>2&&p[1]==L':'&&(p[2]==L'\\'||p[2]==L'/')){
        if(p[0]==L'D'||p[0]==L'd')p=g_titleDirectory+p.substr(2);
    }else if(p[0]==L'\\')p=g_titleDirectory+p;
    return p;
}
DWORD FileAccess(uint32_t a){DWORD r=0;if(a&0x80000000u)r|=GENERIC_READ;if(a&0x40000000u)r|=GENERIC_WRITE;if(a&1)r|=GENERIC_READ;if(a&2)r|=GENERIC_WRITE;return r?r:GENERIC_READ;}
DWORD FileDisposition(uint32_t d){switch(d){case 0:return CREATE_ALWAYS;case 1:return OPEN_EXISTING;case 2:return CREATE_NEW;case 3:return OPEN_ALWAYS;case 4:return TRUNCATE_EXISTING;case 5:return CREATE_ALWAYS;default:return OPEN_EXISTING;}}
LONG NtCreateFileImpl(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,uint32_t attrs,uint32_t share,uint32_t disposition,uint32_t options){
    if(!out||!oa||!oa->ObjectName)return LONG(0xC000000Du);
    std::wstring path=TranslateXboxPath(oa->ObjectName);if(path.empty())return LONG(0xC0000033u);
    DWORD flags=attrs?attrs:FILE_ATTRIBUTE_NORMAL;
    if(options&1)flags|=FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h=CreateFileW(path.c_str(),FileAccess(access),((share&1)?FILE_SHARE_READ:0)|((share&2)?FILE_SHARE_WRITE:0)|((share&4)?FILE_SHARE_DELETE:0),nullptr,FileDisposition(disposition),flags,nullptr);
    if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();if(io){io->Status=LONG(0xC0000001u);io->Information=0;}return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?LONG(0xC0000034u):e==ERROR_ACCESS_DENIED?LONG(0xC0000022u):LONG(0xC0000001u);}
    *out=h;if(io){io->Status=0;io->Information=1;}return 0;
}
extern "C" LONG __stdcall K_NtCreateFile(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,const LARGE_INTEGER*,uint32_t attrs,uint32_t share,uint32_t disposition,uint32_t options){return NtCreateFileImpl(out,access,oa,io,attrs,share,disposition,options);}
extern "C" LONG __stdcall K_NtOpenFile(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,uint32_t share,uint32_t options){return NtCreateFileImpl(out,access,oa,io,FILE_ATTRIBUTE_NORMAL,share,1,options);}
extern "C" LONG __stdcall K_NtReadFile(HANDLE h,HANDLE event,void*,void*,GuestIoStatusBlock*io,void*buffer,uint32_t length,const LARGE_INTEGER*offset,uint32_t*){
    if(offset){LARGE_INTEGER p=*offset;if(!SetFilePointerEx(h,p,nullptr,FILE_BEGIN))return LONG(0xC0000001u);}
    DWORD read=0;if(!ReadFile(h,buffer,length,&read,nullptr)){if(io){io->Status=LONG(0xC0000001u);io->Information=0;}return LONG(0xC0000001u);}
    if(io){io->Status=read?0:LONG(0xC0000011u);io->Information=read;}if(event)SetEvent(event);return read?0:LONG(0xC0000011u);
}
extern "C" LONG __stdcall K_NtWriteFile(HANDLE h,HANDLE event,void*,void*,GuestIoStatusBlock*io,const void*buffer,uint32_t length,const LARGE_INTEGER*offset,uint32_t*){
    if(offset){LARGE_INTEGER p=*offset;if(!SetFilePointerEx(h,p,nullptr,FILE_BEGIN))return LONG(0xC0000001u);}
    DWORD written=0;if(!WriteFile(h,buffer,length,&written,nullptr)){if(io){io->Status=LONG(0xC0000001u);io->Information=0;}return LONG(0xC0000001u);}
    if(io){io->Status=0;io->Information=written;}if(event)SetEvent(event);return 0;
}
extern "C" LONG __stdcall K_NtFlushBuffersFile(HANDLE h,GuestIoStatusBlock*io){BOOL ok=FlushFileBuffers(h);if(io){io->Status=ok?0:LONG(0xC0000001u);io->Information=0;}return ok?0:LONG(0xC0000001u);}
extern "C" LONG __stdcall K_NtClose(HANDLE h){return CloseHandle(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtCreateEvent(HANDLE*out,ACCESS_MASK,void*,uint32_t type,BOOL initial){
    if(!out)return LONG(0xC000000Du);HANDLE h=CreateEventW(nullptr,type==0,initial,nullptr);if(!h)return LONG(0xC0000001u);*out=h;return 0;
}
extern "C" LONG __stdcall K_NtSetEvent(HANDLE h,LONG*previous){if(previous)*previous=0;return SetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtClearEvent(HANDLE h){return ResetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtWaitForSingleObject(HANDLE h,BOOL alertable,const LARGE_INTEGER*timeout){
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=WaitForSingleObject(h,ms);return r==WAIT_OBJECT_0?0:r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
struct GuestAnsiString { USHORT Length,MaximumLength; char* Buffer; };
extern "C" void __stdcall K_RtlInitAnsiString(GuestAnsiString*d,const char*s){if(!d)return;size_t n=s?strlen(s):0;d->Length=USHORT(n>0xFFFE?0xFFFE:n);d->MaximumLength=USHORT(d->Length+(s?1:0));d->Buffer=const_cast<char*>(s);}
extern "C" void __stdcall K_RtlInitUnicodeString(GuestUnicodeString*d,const wchar_t*s){if(!d)return;size_t n=s?wcslen(s)*2:0;d->Length=USHORT(n>0xFFFE?0xFFFE:n);d->MaximumLength=USHORT(d->Length+(s?2:0));d->Buffer=const_cast<wchar_t*>(s);}
extern "C" LONG __stdcall K_RtlEnterCriticalSection(RTL_CRITICAL_SECTION*c){if(!c)return LONG(0xC000000Du);EnterCriticalSection(c);return 0;}
extern "C" void __stdcall K_RtlLeaveCriticalSection(RTL_CRITICAL_SECTION*c){if(c)LeaveCriticalSection(c);}
extern "C" void __stdcall K_RtlInitializeCriticalSection(RTL_CRITICAL_SECTION*c){if(c)InitializeCriticalSection(c);}
extern "C" BOOL __stdcall K_RtlTryEnterCriticalSection(RTL_CRITICAL_SECTION*c){return c?TryEnterCriticalSection(c):FALSE;}
extern "C" LONG __stdcall K_RtlCompareString(const GuestAnsiString*a,const GuestAnsiString*b,BOOL ci){
 if(!a||!b)return 0;size_t n=a->Length<b->Length?a->Length:b->Length;int r=ci?_strnicmp(a->Buffer,b->Buffer,n):strncmp(a->Buffer,b->Buffer,n);return r?r:int(a->Length)-int(b->Length);
}
extern "C" BOOL __stdcall K_RtlEqualString(const GuestAnsiString*a,const GuestAnsiString*b,BOOL ci){return K_RtlCompareString(a,b,ci)==0;}
extern "C" LONG __stdcall K_RtlCompareUnicodeString(const GuestUnicodeString*a,const GuestUnicodeString*b,BOOL ci){
 if(!a||!b)return 0;size_t an=a->Length/2,bn=b->Length/2,n=an<bn?an:bn;int r=ci?_wcsnicmp(a->Buffer,b->Buffer,n):wcsncmp(a->Buffer,b->Buffer,n);return r?r:int(an)-int(bn);
}
extern "C" BOOL __stdcall K_RtlEqualUnicodeString(const GuestUnicodeString*a,const GuestUnicodeString*b,BOOL ci){return K_RtlCompareUnicodeString(a,b,ci)==0;}
extern "C" void __stdcall K_RtlCopyString(GuestAnsiString*d,const GuestAnsiString*s){if(!d||!d->Buffer)return;USHORT n=s?s->Length:0;if(n>d->MaximumLength)n=d->MaximumLength;if(n&&s&&s->Buffer)memmove(d->Buffer,s->Buffer,n);d->Length=n;}
extern "C" void __stdcall K_RtlCopyUnicodeString(GuestUnicodeString*d,const GuestUnicodeString*s){if(!d||!d->Buffer)return;USHORT n=s?s->Length:0;if(n>d->MaximumLength)n=d->MaximumLength&~1u;if(n&&s&&s->Buffer)memmove(d->Buffer,s->Buffer,n);d->Length=n;}
extern "C" LONG __stdcall K_RtlAppendStringToString(GuestAnsiString*d,const GuestAnsiString*s){if(!d||!s||!d->Buffer||!s->Buffer||uint32_t(d->Length)+s->Length>d->MaximumLength)return LONG(0xC0000023u);memmove(d->Buffer+d->Length,s->Buffer,s->Length);d->Length+=s->Length;return 0;}
extern "C" LONG __stdcall K_RtlAppendUnicodeStringToString(GuestUnicodeString*d,const GuestUnicodeString*s){if(!d||!s||!d->Buffer||!s->Buffer||uint32_t(d->Length)+s->Length>d->MaximumLength)return LONG(0xC0000023u);memmove(reinterpret_cast<uint8_t*>(d->Buffer)+d->Length,s->Buffer,s->Length);d->Length+=s->Length;return 0;}
extern "C" LONG __stdcall K_RtlAppendUnicodeToString(GuestUnicodeString*d,const wchar_t*s){if(!d||!s||!d->Buffer)return LONG(0xC000000Du);size_t n=wcslen(s)*2;if(uint32_t(d->Length)+n>d->MaximumLength)return LONG(0xC0000023u);memcpy(reinterpret_cast<uint8_t*>(d->Buffer)+d->Length,s,n);d->Length+=USHORT(n);return 0;}
extern "C" wchar_t __stdcall K_RtlUpcaseUnicodeChar(wchar_t c){return wchar_t(towupper(c));}
extern "C" wchar_t __stdcall K_RtlDowncaseUnicodeChar(wchar_t c){return wchar_t(towlower(c));}
extern "C" char __stdcall K_RtlUpperChar(char c){return char(toupper((unsigned char)c));}
extern "C" char __stdcall K_RtlLowerChar(char c){return char(tolower((unsigned char)c));}
extern "C" void __stdcall K_RtlZeroMemory(void*d,uint32_t n){if(d)std::memset(d,0,n);}
extern "C" void __stdcall K_RtlFillMemory(void*d,uint32_t n,uint8_t v){if(d)std::memset(d,v,n);}
extern "C" void __stdcall K_RtlMoveMemory(void*d,const void*s,uint32_t n){if(d&&s)std::memmove(d,s,n);}
extern "C" uint32_t __stdcall K_RtlCompareMemory(const void*a,const void*b,uint32_t n){const uint8_t*x=(const uint8_t*)a,*y=(const uint8_t*)b;uint32_t i=0;while(i<n&&x[i]==y[i])++i;return i;}
extern "C" uint32_t __fastcall K_RtlUlongByteSwap(uint32_t v){return _byteswap_ulong(v);}
extern "C" uint16_t __fastcall K_RtlUshortByteSwap(uint16_t v){return _byteswap_ushort(v);}
extern "C" void __stdcall K_DbgBreakPoint(){DebugBreak();}
extern "C" void __stdcall K_DbgBreakPointWithStatus(uint32_t){DebugBreak();}
extern "C" uint32_t __cdecl K_DbgPrint(const char*fmt,...){char b[1024];va_list a;va_start(a,fmt);int n=vsnprintf_s(b,sizeof(b),_TRUNCATE,fmt,a);va_end(a);OutputDebugStringA(b);return n<0?0:uint32_t(n);}
extern "C" void __stdcall K_Unsupported(uint32_t ordinal){char b[128];sprintf_s(b,"[XboxKernel] Unsupported import ordinal %u; stopping guest.\n",ordinal);OutputDebugStringA(b);ExitProcess(0xC000001D);}
}
extern "C" __declspec(naked) void KernelDispatch(){
    __asm {
        mov eax, [esp]
        add esp, 4
        cmp eax, 1
        je av_get
        cmp eax, 4
        je av_set
        cmp eax, 5
        je dbg_break
        cmp eax, 6
        je dbg_break_status
        cmp eax, 8
        je dbg_print
        cmp eax, 14
        je pool_alloc
        cmp eax, 15
        je pool_alloc_tag
        cmp eax, 17
        je pool_free
        cmp eax, 23
        je pool_size
        cmp eax, 51
        je icx
        cmp eax, 52
        je idec
        cmp eax, 53
        je iinc
        cmp eax, 54
        je iex
        cmp eax, 55
        je iexa
        cmp eax, 99
        je delay_thread
        cmp eax, 103
        je current_irql
        cmp eax, 104
        je current_thread
        cmp eax, 126
        je qpc
        cmp eax, 127
        je qpf
        cmp eax, 128
        je qst
        cmp eax, 165
        je mm_alloc
        cmp eax, 166
        je mm_alloc_ex
        cmp eax, 171
        je mm_free
        cmp eax, 173
        je mm_phys
        cmp eax, 174
        je mm_valid
        cmp eax, 190
        je nt_create_file
        cmp eax, 202
        je nt_open_file
        cmp eax, 219
        je nt_read_file
        cmp eax, 236
        je nt_write_file
        cmp eax, 198
        je nt_flush
        cmp eax, 187
        je nt_close
        cmp eax, 189
        je nt_event
        cmp eax, 225
        je nt_set_event
        cmp eax, 186
        je nt_clear_event
        cmp eax, 233
        je nt_wait
        cmp eax, 261
        je rtl_append_a
        cmp eax, 262
        je rtl_append_w
        cmp eax, 263
        je rtl_append_wchar
        cmp eax, 270
        je rtl_cmp_a
        cmp eax, 271
        je rtl_cmp_w
        cmp eax, 272
        je rtl_copy_a
        cmp eax, 273
        je rtl_copy_w
        cmp eax, 275
        je rtl_lower_w
        cmp eax, 277
        je rtl_enter_cs
        cmp eax, 279
        je rtl_equal_a
        cmp eax, 280
        je rtl_equal_w
        cmp eax, 289
        je rtl_init_a
        cmp eax, 290
        je rtl_init_w
        cmp eax, 291
        je rtl_init_cs
        cmp eax, 294
        je rtl_leave_cs
        cmp eax, 296
        je rtl_lower_a
        cmp eax, 306
        je rtl_try_cs
        cmp eax, 313
        je rtl_upper_w
        cmp eax, 316
        je rtl_upper_a
        cmp eax, 284
        je rtl_fill
        cmp eax, 298
        je rtl_move
        cmp eax, 307
        je rtl_bswap32
        cmp eax, 318
        je rtl_bswap16
        cmp eax, 320
        je rtl_zero
        push eax
        call K_Unsupported
        add esp, 4
        ret
    av_get: jmp K_AvGetSavedDataAddress
    av_set: jmp K_AvSetSavedDataAddress
    dbg_break: jmp K_DbgBreakPoint
    dbg_break_status: jmp K_DbgBreakPointWithStatus
    dbg_print: jmp K_DbgPrint
    pool_alloc: jmp K_ExAllocatePool
    pool_alloc_tag: jmp K_ExAllocatePoolWithTag
    pool_free: jmp K_ExFreePool
    pool_size: jmp K_ExQueryPoolBlockSize
    icx: jmp K_InterlockedCompareExchange
    idec: jmp K_InterlockedDecrement
    iinc: jmp K_InterlockedIncrement
    iex: jmp K_InterlockedExchange
    iexa: jmp K_InterlockedExchangeAdd
    delay_thread: jmp K_KeDelayExecutionThread
    current_irql: jmp K_KeGetCurrentIrql
    current_thread: jmp K_KeGetCurrentThread
    qpc: jmp K_KeQueryPerformanceCounter
    qpf: jmp K_KeQueryPerformanceFrequency
    qst: jmp K_KeQuerySystemTime
    mm_alloc: jmp K_MmAllocateContiguousMemory
    mm_alloc_ex: jmp K_MmAllocateContiguousMemoryEx
    mm_free: jmp K_MmFreeContiguousMemory
    mm_phys: jmp K_MmGetPhysicalAddress
    mm_valid: jmp K_MmIsAddressValid
    nt_create_file: jmp K_NtCreateFile
    nt_open_file: jmp K_NtOpenFile
    nt_read_file: jmp K_NtReadFile
    nt_write_file: jmp K_NtWriteFile
    nt_flush: jmp K_NtFlushBuffersFile
    nt_close: jmp K_NtClose
    nt_event: jmp K_NtCreateEvent
    nt_set_event: jmp K_NtSetEvent
    nt_clear_event: jmp K_NtClearEvent
    nt_wait: jmp K_NtWaitForSingleObject
    rtl_append_a: jmp K_RtlAppendStringToString
    rtl_append_w: jmp K_RtlAppendUnicodeStringToString
    rtl_append_wchar: jmp K_RtlAppendUnicodeToString
    rtl_cmp_a: jmp K_RtlCompareString
    rtl_cmp_w: jmp K_RtlCompareUnicodeString
    rtl_copy_a: jmp K_RtlCopyString
    rtl_copy_w: jmp K_RtlCopyUnicodeString
    rtl_lower_w: jmp K_RtlDowncaseUnicodeChar
    rtl_enter_cs: jmp K_RtlEnterCriticalSection
    rtl_equal_a: jmp K_RtlEqualString
    rtl_equal_w: jmp K_RtlEqualUnicodeString
    rtl_init_a: jmp K_RtlInitAnsiString
    rtl_init_w: jmp K_RtlInitUnicodeString
    rtl_init_cs: jmp K_RtlInitializeCriticalSection
    rtl_leave_cs: jmp K_RtlLeaveCriticalSection
    rtl_lower_a: jmp K_RtlLowerChar
    rtl_try_cs: jmp K_RtlTryEnterCriticalSection
    rtl_upper_w: jmp K_RtlUpcaseUnicodeChar
    rtl_upper_a: jmp K_RtlUpperChar
    rtl_fill: jmp K_RtlFillMemory
    rtl_move: jmp K_RtlMoveMemory
    rtl_bswap32: jmp K_RtlUlongByteSwap
    rtl_bswap16: jmp K_RtlUshortByteSwap
    rtl_zero: jmp K_RtlZeroMemory
    }
}
namespace {
bool DecodeVa(uint32_t encoded,uint32_t key,uint32_t&va){va=encoded^key;return InImage(va,4);}
}
void XboxKernelSetTitlePath(const wchar_t* path){
    g_titleDirectory.clear();if(!path)return;g_titleDirectory=path;size_t p=g_titleDirectory.find_last_of(L"\\/");if(p!=std::wstring::npos)g_titleDirectory.resize(p);
}
bool XboxKernelResolveImports(const XboxKernelState& image){
    g_image=reinterpret_cast<uint8_t*>(uintptr_t(image.imageBase));g_base=image.imageBase;g_size=image.imageSize;g_entry=image.entryPoint;g_tls=image.tlsAddress;
    if(!g_image||!InImage(g_entry))return false;
    const uint32_t encoded=*reinterpret_cast<uint32_t*>(g_image+0x158);
    uint32_t thunk=0;if(!DecodeVa(encoded,kRetailThunk,thunk)&&!DecodeVa(encoded,kDebugThunk,thunk)){
        // Some homebrew images have no kernel imports.
        if(encoded==0||encoded==kRetailThunk||encoded==kDebugThunk)return true;return false;
    }
    g_stubPage=VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!g_stubPage)return false;
    uint32_t* table=reinterpret_cast<uint32_t*>(uintptr_t(thunk));
    for(uint32_t i=0;i<kThunkMax;i++){
        if(!InImage(thunk+i*4,4))return false;
        uint32_t v=table[i];if(v==0){FlushInstructionCache(GetCurrentProcess(),g_stubPage,g_stubCount*10);return true;}
        if((v&0x80000000u)==0)return false;
        uint32_t ordinal=v&0x7FFFFFFFu;if(ordinal>=kThunkMax)return false;
        void* target=DataFor(ordinal);
        if(!target)target=StubFor(ordinal);
        if(!target)return false;
        table[i]=uint32_t(reinterpret_cast<uintptr_t>(target));
    }
    return false;
}
DWORD WINAPI XboxKernelStartXbe(void*){
    if(!g_entry)return ERROR_INVALID_ADDRESS;
    DWORD tlsIndex=TLS_OUT_OF_INDEXES;void* tlsBlock=nullptr;
    if(g_tls){
        struct TlsDirectory { uint32_t start,end,index,callbacks,zeroFill,characteristics; };
        if(!InImage(g_tls,sizeof(TlsDirectory)))return ERROR_INVALID_ADDRESS;
        auto*t=reinterpret_cast<TlsDirectory*>(uintptr_t(g_tls));
        if(t->end<t->start||!InImage(t->start,t->end-t->start)||!InImage(t->index,4))return ERROR_INVALID_ADDRESS;
        tlsIndex=TlsAlloc();if(tlsIndex==TLS_OUT_OF_INDEXES)return ERROR_NOT_ENOUGH_MEMORY;
        uint64_t total=uint64_t(t->end-t->start)+t->zeroFill;
        if(total>0x1000000){TlsFree(tlsIndex);return ERROR_INVALID_DATA;}
        tlsBlock=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size_t(total?total:1));
        if(!tlsBlock){TlsFree(tlsIndex);return ERROR_NOT_ENOUGH_MEMORY;}
        if(t->end>t->start)std::memcpy(tlsBlock,reinterpret_cast<void*>(uintptr_t(t->start)),t->end-t->start);
        *reinterpret_cast<uint32_t*>(uintptr_t(t->index))=tlsIndex;
        if(!TlsSetValue(tlsIndex,tlsBlock)){HeapFree(GetProcessHeap(),0,tlsBlock);TlsFree(tlsIndex);return GetLastError();}
        if(t->callbacks){
            if(!InImage(t->callbacks,4)){HeapFree(GetProcessHeap(),0,tlsBlock);TlsFree(tlsIndex);return ERROR_INVALID_ADDRESS;}
            auto callbacks=reinterpret_cast<uint32_t*>(uintptr_t(t->callbacks));
            for(uint32_t i=0;i<64&&InImage(t->callbacks+i*4,4)&&callbacks[i];++i){
                using Callback=void (NTAPI*)(void*,DWORD,void*);
                reinterpret_cast<Callback>(uintptr_t(callbacks[i]))(g_image,DLL_PROCESS_ATTACH,nullptr);
            }
        }
    }
    using Entry=void(__cdecl*)();
    DWORD result=0;
    __try { reinterpret_cast<Entry>(uintptr_t(g_entry))(); }
    __except(EXCEPTION_EXECUTE_HANDLER){OutputDebugStringW(L"[XboxKernel] XBE entry raised a structured exception.\n");result=GetExceptionCode();}
    if(tlsIndex!=TLS_OUT_OF_INDEXES){TlsSetValue(tlsIndex,nullptr);if(tlsBlock)HeapFree(GetProcessHeap(),0,tlsBlock);TlsFree(tlsIndex);}
    return result;
}
