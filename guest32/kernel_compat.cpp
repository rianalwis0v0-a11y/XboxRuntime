#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "kernel_compat.h"

namespace {
constexpr uint32_t kRetailEntry=0xA8FC57ABu, kDebugEntry=0x94859D4Bu;
constexpr uint32_t kRetailThunk=0x5B6D40B6u, kDebugThunk=0xEFB1F152u;
constexpr uint32_t kThunkMax=379;
struct XbeHeaderView { uint8_t bytes[0x178]; };
static uint8_t* g_image=nullptr;
static uint32_t g_base=0,g_size=0,g_entry=0;
static void* g_stubPage=nullptr;
static uint32_t g_stubCount=0;
static void* g_savedAv=nullptr;
static volatile LONG g_tick=0;
static uint32_t g_data[64]{};

bool InImage(uint32_t va,uint32_t n=4){return va>=g_base&&uint64_t(va)+n<=uint64_t(g_base)+g_size;}
uint32_t Read32(uint32_t va){return *reinterpret_cast<uint32_t*>(uintptr_t(va));}
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
        return &g_data[ordinal%64];
    default:return nullptr;
    }
}
extern "C" void* __stdcall K_ExAllocatePool(uint32_t n){return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n?n:1);}
extern "C" void* __stdcall K_ExAllocatePoolWithTag(uint32_t n,uint32_t){return K_ExAllocatePool(n);}
extern "C" void __stdcall K_ExFreePool(void*p){if(p)HeapFree(GetProcessHeap(),0,p);}
extern "C" uint32_t __stdcall K_ExQueryPoolBlockSize(void*p){return p?HeapSize(GetProcessHeap(),0,p):0xFFFFFFFFu;}
extern "C" void* __stdcall K_AvGetSavedDataAddress(){return g_savedAv;}
extern "C" void __stdcall K_AvSetSavedDataAddress(void*p){g_savedAv=p;}
extern "C" LONG __stdcall K_InterlockedCompareExchange(volatile LONG*p,LONG e,LONG c){return InterlockedCompareExchange(p,e,c);}
extern "C" LONG __stdcall K_InterlockedDecrement(volatile LONG*p){return InterlockedDecrement(p);}
extern "C" LONG __stdcall K_InterlockedIncrement(volatile LONG*p){return InterlockedIncrement(p);}
extern "C" LONG __stdcall K_InterlockedExchange(volatile LONG*p,LONG v){return InterlockedExchange(p,v);}
extern "C" LONG __stdcall K_InterlockedExchangeAdd(volatile LONG*p,LONG v){return InterlockedExchangeAdd(p,v);}
extern "C" DWORD __stdcall K_KeGetCurrentIrql(){return 0;}
extern "C" HANDLE __stdcall K_KeGetCurrentThread(){return GetCurrentThread();}
extern "C" void __stdcall K_KeDelayExecutionThread(uint32_t,BOOL, const LARGE_INTEGER* interval){
    if(!interval){Sleep(0);return;}
    LONGLONG ticks=interval->QuadPart;
    if(ticks<0){ULONGLONG ms=ULONGLONG(-ticks)/10000ull;Sleep(DWORD(ms>MAXDWORD?MAXDWORD:ms));}
    else Sleep(0);
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
extern "C" LONG __stdcall K_NtClose(HANDLE h){return CloseHandle(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtCreateEvent(HANDLE*out,ACCESS_MASK,void*,uint32_t type,BOOL initial){
    if(!out)return LONG(0xC000000Du);HANDLE h=CreateEventW(nullptr,type!=0,initial,nullptr);if(!h)return LONG(0xC0000001u);*out=h;return 0;
}
extern "C" LONG __stdcall K_NtSetEvent(HANDLE h,LONG*previous){if(previous)*previous=0;return SetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtClearEvent(HANDLE h){return ResetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtWaitForSingleObject(HANDLE h,BOOL alertable,const LARGE_INTEGER*timeout){
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=WaitForSingleObject(h,ms);return r==WAIT_OBJECT_0?0:r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
extern "C" void __stdcall K_RtlZeroMemory(void*d,uint32_t n){if(d)std::memset(d,0,n);}
extern "C" void __stdcall K_RtlFillMemory(void*d,uint32_t n,uint8_t v){if(d)std::memset(d,v,n);}
extern "C" void __stdcall K_RtlMoveMemory(void*d,const void*s,uint32_t n){if(d&&s)std::memmove(d,s,n);}
extern "C" uint32_t __stdcall K_RtlCompareMemory(const void*a,const void*b,uint32_t n){const uint8_t*x=(const uint8_t*)a,*y=(const uint8_t*)b;uint32_t i=0;while(i<n&&x[i]==y[i])++i;return i;}
extern "C" uint32_t __stdcall K_RtlUlongByteSwap(uint32_t v){return _byteswap_ulong(v);}
extern "C" uint16_t __stdcall K_RtlUshortByteSwap(uint16_t v){return _byteswap_ushort(v);}
extern "C" void __stdcall K_DbgBreakPoint(){DebugBreak();}
extern "C" void __stdcall K_DbgBreakPointWithStatus(uint32_t){DebugBreak();}
extern "C" uint32_t __cdecl K_DbgPrint(const char*fmt,...){char b[1024];va_list a;va_start(a,fmt);int n=vsnprintf_s(b,sizeof(b),_TRUNCATE,fmt,a);va_end(a);OutputDebugStringA(b);return n<0?0:uint32_t(n);}
extern "C" void __stdcall K_Unsupported(uint32_t ordinal){char b[128];sprintf_s(b,"[XboxKernel] Unsupported import ordinal %u; stopping guest.\n",ordinal);OutputDebugStringA(b);ExitProcess(0xC000001D);}
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
    nt_close: jmp K_NtClose
    nt_event: jmp K_NtCreateEvent
    nt_set_event: jmp K_NtSetEvent
    nt_clear_event: jmp K_NtClearEvent
    nt_wait: jmp K_NtWaitForSingleObject
    rtl_fill: jmp K_RtlFillMemory
    rtl_move: jmp K_RtlMoveMemory
    rtl_bswap32: jmp K_RtlUlongByteSwap
    rtl_bswap16: jmp K_RtlUshortByteSwap
    rtl_zero: jmp K_RtlZeroMemory
    }
}
bool DecodeVa(uint32_t encoded,uint32_t key,uint32_t&va){va=encoded^key;return InImage(va,4);}
}
bool XboxKernelResolveImports(const XboxKernelState& image){
    g_image=reinterpret_cast<uint8_t*>(uintptr_t(image.imageBase));g_base=image.imageBase;g_size=image.imageSize;g_entry=image.entryPoint;
    if(!g_image||!InImage(g_entry))return false;
    const uint32_t encoded=*reinterpret_cast<uint32_t*>(g_image+0x158);
    uint32_t thunk=0;if(!DecodeVa(encoded,kRetailThunk,thunk)&&!DecodeVa(encoded,kDebugThunk,thunk)){
        // Some homebrew images have no kernel imports.
        if(encoded==0)return true;return false;
    }
    g_stubPage=VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!g_stubPage)return false;
    uint32_t* table=reinterpret_cast<uint32_t*>(uintptr_t(thunk));
    for(uint32_t i=0;i<kThunkMax;i++){
        if(!InImage(thunk+i*4,4))return false;
        uint32_t v=table[i];if(v==0)return true;
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
    using Entry=void(__cdecl*)();
    __try { reinterpret_cast<Entry>(uintptr_t(g_entry))(); }
    __except(EXCEPTION_EXECUTE_HANDLER){OutputDebugStringW(L"[XboxKernel] XBE entry raised a structured exception.\n");return GetExceptionCode();}
    return 0;
}
