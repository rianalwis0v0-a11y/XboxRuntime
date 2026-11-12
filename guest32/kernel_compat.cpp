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
static LONG RtlCaseUnicodeString(GuestUnicodeString* destination, const GuestUnicodeString* source, BOOL allocate, bool upper) {
    if (!destination || !source || (!source->Buffer && source->Length) || (source->Length & 1))
        return LONG(0xC000000Du);
    const ULONG bytes = source->Length;
    if (allocate) {
        if (bytes > 0xFFFD) return LONG(0xC0000023u);
        wchar_t* buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(wchar_t)));
        if (!buffer) return LONG(0xC0000017u);
        destination->Buffer = buffer;
        destination->MaximumLength = USHORT(bytes + sizeof(wchar_t));
    } else if (!destination->Buffer || destination->MaximumLength < bytes) {
        return LONG(0xC0000023u);
    }
    const ULONG chars = bytes / sizeof(wchar_t);
    for (ULONG i = 0; i < chars; ++i) {
        const wchar_t c = source->Buffer[i];
        destination->Buffer[i] = upper ? wchar_t(towupper(c)) : wchar_t(towlower(c));
    }
    if (destination->MaximumLength >= bytes + sizeof(wchar_t)) destination->Buffer[chars] = L'\\0';
    destination->Length = USHORT(bytes);
    return 0;
}
extern "C" LONG __stdcall K_RtlDowncaseUnicodeString(GuestUnicodeString* d, const GuestUnicodeString* s, BOOL allocate) {
    return RtlCaseUnicodeString(d, s, allocate, false);
}
extern "C" LONG __stdcall K_RtlUpcaseUnicodeString(GuestUnicodeString* d, const GuestUnicodeString* s, BOOL allocate) {
    return RtlCaseUnicodeString(d, s, allocate, true);
}
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
extern "C" LONG __stdcall K_NtAllocateVirtualMemory(void** base, ULONG_PTR, SIZE_T* size, ULONG type, ULONG protect) {
    if (!base || !size || !*size) return LONG(0xC000000Du);
    void* p = VirtualAlloc(*base, *size, type, protect);
    if (!p) return LONG(0xC0000017u);
    *base = p;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(p, &info, sizeof(info))) *size = info.RegionSize;
    return 0;
}
extern "C" LONG __stdcall K_NtProtectVirtualMemory(void** base,SIZE_T* size,ULONG protect,ULONG* oldProtect){
    if(!base||!*base||!size||!*size)return LONG(0xC000000Du);DWORD old=0;
    if(!VirtualProtect(*base,*size,protect,&old))return LONG(0xC000000Du);
    if(oldProtect)*oldProtect=old;return 0;
}
extern "C" LONG __stdcall K_NtQueryVirtualMemory(void* base,ULONG infoClass,void* out,ULONG length,ULONG* returned){
    if(infoClass!=0||!base||!out)return LONG(0xC0000003u);
    MEMORY_BASIC_INFORMATION info{};SIZE_T n=VirtualQuery(base,&info,sizeof(info));if(!n)return LONG(0xC000000Du);
    if(returned)*returned=ULONG(n);if(length<sizeof(info))return LONG(0xC0000023u);
    std::memcpy(out,&info,sizeof(info));return 0;
}
extern "C" LONG __stdcall K_NtCreateSemaphore(HANDLE* out,void*,LONG initial,LONG maximum){
    if(!out||maximum<=0||initial<0||initial>maximum)return LONG(0xC000000Du);
    HANDLE h=CreateSemaphoreW(nullptr,initial,maximum,nullptr);if(!h)return LONG(0xC0000017u);*out=h;return 0;
}
extern "C" LONG __stdcall K_NtReleaseSemaphore(HANDLE h,LONG count,LONG* previous){
    if(count<=0)return LONG(0xC000000Du);LONG p=0;BOOL ok=ReleaseSemaphore(h,count,&p);if(ok&&previous)*previous=p;return ok?0:LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtWaitForMultipleObjects(ULONG count,const HANDLE* handles,ULONG waitType,BOOL alertable,const LARGE_INTEGER* timeout,void*){
    if(!handles||!count||count>MAXIMUM_WAIT_OBJECTS||waitType>1)return LONG(0xC000000Du);
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=WaitForMultipleObjects(count,handles,waitType==0,ms);if(r>=WAIT_OBJECT_0&&r<WAIT_OBJECT_0+count)return 0;
    if(r>=WAIT_ABANDONED_0&&r<WAIT_ABANDONED_0+count)return LONG(0x80);
    return r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtSignalAndWaitForSingleObject(HANDLE signal,HANDLE wait,BOOL alertable,const LARGE_INTEGER* timeout){
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=SignalObjectAndWait(signal,wait,ms,FALSE);return r==WAIT_OBJECT_0?0:r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtFreeVirtualMemory(void** base, SIZE_T* size, ULONG type) {
    if (!base || !*base) return LONG(0xC000000Du);
    SIZE_T n = size ? *size : 0;
    if (!VirtualFree(*base, n, type)) return LONG(0xC000000Du);
    if (type & MEM_RELEASE) { *base = nullptr; if (size) *size = 0; }
    return 0;
}

extern "C" void __stdcall K_KeInitializeEvent(void* event, ULONG type, BOOL initialState) {
    if (!event) return;
    HANDLE h = CreateEventW(nullptr, type == 0, initialState, nullptr);
    if (h) StoreKernelObject(event, h);
}
extern "C" LONG __stdcall K_KeSetEvent(void* event, LONG, BOOL) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    return SetEvent(h) ? previous : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KeResetEvent(void* event) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    return ResetEvent(h) ? previous : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KePulseEvent(void* event, LONG, BOOL) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    if (!PulseEvent(h)) return LONG(0xC0000008u);
    return previous;
}
extern "C" LONG __stdcall K_KeWaitForSingleObject(void* object, ULONG, ULONG, BOOL, const LARGE_INTEGER* timeout) {
    HANDLE h = FindKernelObject(object);
    if (!h) return LONG(0xC0000008u);
    const DWORD result = WaitForSingleObject(h, KernelTimeout(timeout));
    return result == WAIT_OBJECT_0 ? 0 : result == WAIT_TIMEOUT ? LONG(0x102) : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KeWaitForMultipleObjects(ULONG count, void** objects, ULONG waitType, ULONG, ULONG,
                                                       BOOL, const LARGE_INTEGER* timeout, void*) {
    if (!objects || !count || count > MAXIMUM_WAIT_OBJECTS || waitType > 1) return LONG(0xC000000Du);
    HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
    for (ULONG i = 0; i < count; ++i) {
        handles[i] = FindKernelObject(objects[i]);
        if (!handles[i]) return LONG(0xC0000008u);
    }
    const DWORD result = WaitForMultipleObjects(count, handles, waitType == 0, KernelTimeout(timeout));
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) return LONG(result - WAIT_OBJECT_0);
    if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) return LONG(0x80);
    return result == WAIT_TIMEOUT ? LONG(0x102) : LONG(0xC0000008u);
}
extern "C" void __stdcall K_KeInitializeSemaphore(void* semaphore, LONG count, LONG limit) {
    if (!semaphore || limit <= 0 || count < 0 || count > limit) return;
    HANDLE h = CreateSemaphoreW(nullptr, count, limit, nullptr);
    if (h) StoreKernelObject(semaphore, h);
}
extern "C" LONG __stdcall K_KeReleaseSemaphore(void* semaphore, LONG, LONG adjustment, BOOL) {
    HANDLE h = FindKernelObject(semaphore);
    if (!h || adjustment <= 0) return LONG(0xC000000Du);
    LONG previous = 0;
    return ReleaseSemaphore(h, adjustment, &previous) ? previous : LONG(0xC0000008u);
}
extern "C" void __stdcall K_KeInitializeMutant(void* mutant, BOOL initialOwner) {
    if (!mutant) return;
    HANDLE h = CreateMutexW(nullptr, initialOwner, nullptr);
    if (h) StoreKernelObject(mutant, h);
}
extern "C" LONG __stdcall K_KeReleaseMutant(void* mutant, LONG, BOOL, BOOL) {
    HANDLE h = FindKernelObject(mutant);
    if (!h) return LONG(0xC0000008u);
    return ReleaseMutex(h) ? 0 : LONG(0xC0000008u);
}


extern "C" void __stdcall K_ExInitializeReadWriteLock(void* lock) { (void)GetGuestRwLock(lock, true); }
extern "C" void __stdcall K_ExAcquireReadWriteLockExclusive(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, true);
    if (native) { AcquireSRWLockExclusive(native); g_heldRwLocks[lock] = true; }
}
extern "C" void __stdcall K_ExAcquireReadWriteLockShared(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, true);
    if (native) { AcquireSRWLockShared(native); g_heldRwLocks[lock] = false; }
}
extern "C" void __stdcall K_ExReleaseReadWriteLock(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, false);
    if (!native) return;
    auto it = g_heldRwLocks.find(lock);
    if (it == g_heldRwLocks.end()) return;
    const bool exclusive = it->second;
    g_heldRwLocks.erase(it);
    if (exclusive) ReleaseSRWLockExclusive(native);
    else ReleaseSRWLockShared(native);
}
extern "C" void __stdcall K_KeEnterCriticalRegion() { ++g_criticalRegionDepth; }
extern "C" void __stdcall K_KeLeaveCriticalRegion() { if (g_criticalRegionDepth) --g_criticalRegionDepth; }
extern "C" UCHAR __fastcall K_KfRaiseIrql(UCHAR level) {
    const UCHAR previous = UCHAR(g_guestIrql);
    if (level > g_guestIrql) g_guestIrql = level;
    return previous;
}
extern "C" void __fastcall K_KfLowerIrql(UCHAR level) { g_guestIrql = level; }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToDpcLevel() { return K_KfRaiseIrql(2); }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToSynchLevel() { return K_KfRaiseIrql(1); }
extern "C" void __stdcall K_KeInitializeTimerEx(void* timer, ULONG type) {
    if (!timer) return;
    HANDLE h = CreateWaitableTimerW(nullptr, type == 0, nullptr);
    if (h) StoreKernelObject(timer, h);
}
extern "C" BOOL __stdcall K_KeSetTimer(void* timer, const LARGE_INTEGER* dueTime, void*) {
    HANDLE h = FindKernelObject(timer);
    return h && dueTime ? SetWaitableTimer(h, dueTime, 0, nullptr, nullptr, FALSE) : FALSE;
}
extern "C" BOOL __stdcall K_KeSetTimerEx(void* timer, const LARGE_INTEGER* dueTime, LONG period, void*) {
    HANDLE h = FindKernelObject(timer);
    return h && dueTime && period >= 0 ? SetWaitableTimer(h, dueTime, period, nullptr, nullptr, FALSE) : FALSE;
}
extern "C" BOOL __stdcall K_KeCancelTimer(void* timer) {
    HANDLE h = FindKernelObject(timer);
    return h ? CancelWaitableTimer(h) : FALSE;
}
extern "C" uint64_t __stdcall K_KeQueryInterruptTime() {
    return GetTickCount64() * 10000ull;
}
extern "C" void __stdcall K_KeStallExecutionProcessor(uint32_t microseconds) {
    LARGE_INTEGER start{}, now{}, frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const uint64_t target = (uint64_t(frequency.QuadPart) * microseconds) / 1000000ull;
    do { QueryPerformanceCounter(&now); YieldProcessor(); }
    while (uint64_t(now.QuadPart - start.QuadPart) < target);
}
extern "C" LONG __stdcall K_NtYieldExecution() {
    SwitchToThread();
    return 0;
}
extern "C" uint32_t __stdcall K_MmQueryAddressProtect(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) ? info.Protect : 0;
}
extern "C" uint32_t __stdcall K_MmQueryAllocationSize(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) ? uint32_t(info.RegionSize) : 0;
}
extern "C" void __stdcall K_MmSetAddressProtect(void* address, uint32_t size, uint32_t protect) {
    DWORD oldProtect = 0;
    if (address && size) VirtualProtect(address, size, protect, &oldProtect);
}
struct GuestTimeFields { SHORT Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; };
extern "C" BOOL __stdcall K_RtlTimeFieldsToTime(const GuestTimeFields* fields, LARGE_INTEGER* time) {
    if (!fields || !time) return FALSE;
    SYSTEMTIME st{};
    st.wYear = WORD(fields->Year); st.wMonth = WORD(fields->Month); st.wDay = WORD(fields->Day);
    st.wHour = WORD(fields->Hour); st.wMinute = WORD(fields->Minute); st.wSecond = WORD(fields->Second);
    st.wMilliseconds = WORD(fields->Milliseconds);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return FALSE;
    ULARGE_INTEGER value{}; value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime;
    time->QuadPart = LONGLONG(value.QuadPart);
    return TRUE;
}
extern "C" void __stdcall K_RtlTimeToTimeFields(const LARGE_INTEGER* time, GuestTimeFields* fields) {
    if (!time || !fields) return;
    ULARGE_INTEGER value{}; value.QuadPart = ULONGLONG(time->QuadPart);
    FILETIME ft{}; ft.dwLowDateTime = value.LowPart; ft.dwHighDateTime = value.HighPart;
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) { std::memset(fields, 0, sizeof(*fields)); return; }
    fields->Year = SHORT(st.wYear); fields->Month = SHORT(st.wMonth); fields->Day = SHORT(st.wDay);
    fields->Hour = SHORT(st.wHour); fields->Minute = SHORT(st.wMinute); fields->Second = SHORT(st.wSecond);
    fields->Milliseconds = SHORT(st.wMilliseconds); fields->Weekday = SHORT(st.wDayOfWeek);
}
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
        cmp eax, 45
        je smbus_read
        cmp eax, 50
        je smbus_write
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
        cmp eax, 12
        je ex_acquire_rw_exclusive
        cmp eax, 13
        je ex_acquire_rw_shared
        cmp eax, 18
        je ex_init_rw
        cmp eax, 28
        je ex_release_rw
                cmp eax, 97
        je ke_cancel_timer
        cmp eax, 101
        je ke_enter_critical
        cmp eax, 113
        je ke_init_timer
        cmp eax, 122
        je ke_leave_critical
        cmp eax, 129
        je ke_raise_dpc
        cmp eax, 130
        je ke_raise_synch
        cmp eax, 149
        je ke_set_timer
        cmp eax, 150
        je ke_set_timer_ex
        cmp eax, 160
        je kf_raise_irql
        cmp eax, 161
        je kf_lower_irql
                cmp eax, 108
        je ke_init_event
        cmp eax, 110
        je ke_init_mutant
        cmp eax, 112
        je ke_init_semaphore
        cmp eax, 123
        je ke_pulse_event
        cmp eax, 132
        je ke_release_semaphore
        cmp eax, 131
        je ke_release_mutant
        cmp eax, 138
        je ke_reset_event
        cmp eax, 145
        je ke_set_event
        cmp eax, 158
        je ke_wait_multiple
        cmp eax, 159
        je ke_wait_single
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
        cmp eax, 125
        je query_interrupt
        cmp eax, 151
        je stall_processor
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
        cmp eax, 179
        je mm_query_protect
        cmp eax, 180
        je mm_query_size
        cmp eax, 182
        je mm_set_protect
        cmp eax, 184
        je nt_alloc_vm
        cmp eax, 199
        je nt_free_vm
        cmp eax, 238
        je nt_yield
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
        cmp eax, 268
        je rtl_cmp_memory
        cmp eax, 278
        je rtl_enter_cs_region
        cmp eax, 295
        je rtl_leave_cs_region
                cmp eax, 270
        je rtl_cmp_a
        cmp eax, 271
        je rtl_cmp_w
        cmp eax, 272
        je rtl_copy_a
        cmp eax, 274
        je rtl_create_unicode
        cmp eax, 273
        je rtl_copy_w
        cmp eax, 276
        je rtl_downcase_unicode_string
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
        cmp eax, 314
        je rtl_upcase_unicode_string
        cmp eax, 315
        je rtl_upcase_unicode_mb
        cmp eax, 317
        je rtl_upper_string
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
        cmp eax, 267
        je rtl_char_to_integer
        cmp eax, 292
        je rtl_integer_to_char
        cmp eax, 293
        je rtl_integer_to_unicode
        cmp eax, 300
        je rtl_mb_to_unicode_size
        cmp eax, 299
        je rtl_mb_to_unicode
        cmp eax, 309
        je rtl_unicode_to_integer
        cmp eax, 311
        je rtl_unicode_to_mb_size
        cmp eax, 310
        je rtl_unicode_to_mb
        cmp eax, 260
        je rtl_ansi_to_unicode
        cmp eax, 269
        je rtl_cmp_mem_ulong
        cmp eax, 285
        je rtl_fill_ulong
        cmp eax, 286
        je rtl_free_ansi
        cmp eax, 287
        je rtl_free_unicode
        cmp eax, 297
        je rtl_map_generic
        cmp eax, 301
        je rtl_status_dos
        cmp eax, 304
        je rtl_time_fields_to_time
        cmp eax, 305
        je rtl_time_to_fields
        cmp eax, 308
        je rtl_unicode_to_ansi
        cmp eax, 204
        je nt_protect_vm
        cmp eax, 217
        je nt_query_vm
        cmp eax, 193
        je nt_create_semaphore
        cmp eax, 222
        je nt_release_semaphore
        cmp eax, 235
        je nt_wait_multiple
        cmp eax, 230
        je nt_signal_wait
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
    smbus_read: jmp K_HalReadSMBusValue
    smbus_write: jmp K_HalWriteSMBusValue
    icx: jmp K_InterlockedCompareExchange
    idec: jmp K_InterlockedDecrement
    iinc: jmp K_InterlockedIncrement
    iex: jmp K_InterlockedExchange
    iexa: jmp K_InterlockedExchangeAdd
    ex_acquire_rw_exclusive: jmp K_ExAcquireReadWriteLockExclusive
    ex_acquire_rw_shared: jmp K_ExAcquireReadWriteLockShared
    ex_init_rw: jmp K_ExInitializeReadWriteLock
    ex_release_rw: jmp K_ExReleaseReadWriteLock
    ke_cancel_timer: jmp K_KeCancelTimer
    ke_enter_critical: jmp K_KeEnterCriticalRegion
    ke_init_timer: jmp K_KeInitializeTimerEx
    ke_leave_critical: jmp K_KeLeaveCriticalRegion
    ke_raise_dpc: jmp K_KeRaiseIrqlToDpcLevel
    ke_raise_synch: jmp K_KeRaiseIrqlToSynchLevel
    ke_set_timer: jmp K_KeSetTimer
    ke_set_timer_ex: jmp K_KeSetTimerEx
    kf_raise_irql: jmp K_KfRaiseIrql
    kf_lower_irql: jmp K_KfLowerIrql
    ke_init_event: jmp K_KeInitializeEvent
    ke_init_mutant: jmp K_KeInitializeMutant
    ke_init_semaphore: jmp K_KeInitializeSemaphore
    ke_pulse_event: jmp K_KePulseEvent
    ke_release_semaphore: jmp K_KeReleaseSemaphore
    ke_release_mutant: jmp K_KeReleaseMutant
    ke_reset_event: jmp K_KeResetEvent
    ke_set_event: jmp K_KeSetEvent
    ke_wait_multiple: jmp K_KeWaitForMultipleObjects
    ke_wait_single: jmp K_KeWaitForSingleObject
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
    query_interrupt: jmp K_KeQueryInterruptTime
    stall_processor: jmp K_KeStallExecutionProcessor
    mm_query_protect: jmp K_MmQueryAddressProtect
    mm_query_size: jmp K_MmQueryAllocationSize
    mm_set_protect: jmp K_MmSetAddressProtect
    nt_alloc_vm: jmp K_NtAllocateVirtualMemory
    nt_free_vm: jmp K_NtFreeVirtualMemory
    nt_yield: jmp K_NtYieldExecution
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
    rtl_cmp_memory: jmp K_RtlCompareMemory
    rtl_create_unicode: jmp K_RtlCreateUnicodeString
    rtl_upcase_unicode_mb: jmp K_RtlUpcaseUnicodeToMultiByteN
    rtl_upper_string: jmp K_RtlUpperString
    rtl_cmp_w: jmp K_RtlCompareUnicodeString
    rtl_copy_a: jmp K_RtlCopyString
    rtl_copy_w: jmp K_RtlCopyUnicodeString
    rtl_lower_w: jmp K_RtlDowncaseUnicodeChar
    rtl_downcase_unicode_string: jmp K_RtlDowncaseUnicodeString
    rtl_upcase_unicode_string: jmp K_RtlUpcaseUnicodeString
    rtl_enter_cs: jmp K_RtlEnterCriticalSection
    rtl_enter_cs_region: jmp K_RtlEnterCriticalSectionAndRegion
    rtl_leave_cs_region: jmp K_RtlLeaveCriticalSectionAndRegion
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

    rtl_char_to_integer: jmp K_RtlCharToInteger
    rtl_integer_to_char: jmp K_RtlIntegerToChar
    rtl_integer_to_unicode: jmp K_RtlIntegerToUnicodeString
    rtl_mb_to_unicode: jmp K_RtlMultiByteToUnicodeN
    rtl_unicode_to_integer: jmp K_RtlUnicodeStringToInteger
    rtl_unicode_to_mb: jmp K_RtlUnicodeToMultiByteN
    rtl_mb_to_unicode_size: jmp K_RtlMultiByteToUnicodeSize
    rtl_unicode_to_mb_size: jmp K_RtlUnicodeToMultiByteSize
    rtl_ansi_to_unicode: jmp K_RtlAnsiStringToUnicodeString
    rtl_cmp_mem_ulong: jmp K_RtlCompareMemoryUlong
    rtl_fill_ulong: jmp K_RtlFillMemoryUlong
    rtl_free_ansi: jmp K_RtlFreeAnsiString
    rtl_free_unicode: jmp K_RtlFreeUnicodeString
    rtl_map_generic: jmp K_RtlMapGenericMask
    rtl_status_dos: jmp K_RtlNtStatusToDosError
    rtl_time_fields_to_time: jmp K_RtlTimeFieldsToTime
    rtl_time_to_fields: jmp K_RtlTimeToTimeFields
    rtl_unicode_to_ansi: jmp K_RtlUnicodeStringToAnsiString
    nt_protect_vm: jmp K_NtProtectVirtualMemory
    nt_query_vm: jmp K_NtQueryVirtualMemory
    nt_create_semaphore: jmp K_NtCreateSemaphore
    nt_release_semaphore: jmp K_NtReleaseSemaphore
    nt_wait_multiple: jmp K_NtWaitForMultipleObjects
    nt_signal_wait: jmp K_NtSignalAndWaitForSingleObject
    }
}
namespace {
bool DecodeVa(uint32_t encoded,uint32_t key,uint32_t&va){va=encoded^key;return InImage(va,4);}
}
bool XboxKernelInitializeEeprom(const wchar_t* path) {
    if (!path || !*path) return false;
    g_eepromPath = path;
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) != FALSE;
    if (ok && size.QuadPart == 0) {
        // Blank EEPROM is intentionally unprovisioned, not a fabricated Xbox identity.
        g_eeprom.fill(0xFF);
        DWORD written = 0;
        ok = WriteFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &written, nullptr) &&
             written == g_eeprom.size() && FlushFileBuffers(f);
    } else if (ok && size.QuadPart == static_cast<LONGLONG>(g_eeprom.size())) {
        DWORD read = 0;
        ok = SetFilePointer(f, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
             ReadFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &read, nullptr) &&
             read == g_eeprom.size();
    } else {
        ok = false; // Never silently truncate or replace a user's EEPROM backup.
    }
    CloseHandle(f);
    g_eepromReady = ok;
    if (ok) OutputDebugStringW(L"[XboxEEPROM] Persistent 256-byte EEPROM loaded. A blank all-FF image is unprovisioned and cannot register with Insignia.\\n");
    return ok;
}

void XboxKernelFlushEeprom() {
    if (!g_eepromReady || g_eepromPath.empty()) return;
    std::lock_guard<std::mutex> lock(g_eepromMutex);
    HANDLE f = CreateFileW(g_eepromPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &written, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
}

void XboxKernelSetTitlePath(const wchar_t* path){
    g_titleDirectory.clear();if(!path)return;g_titleDirectory=path;size_t p=g_titleDirectory.find_last_of(L"\\/");if(p!=std::wstring::npos)g_titleDirectory.resize(p);
}
bool XboxKernelResolveImports(const XboxKernelState& image){
    g_image=reinterpret_cast<uint8_t*>(uintptr_t(image.imageBase));g_base=image.imageBase;g_size=image.imageSize;g_entry=image.entryPoint;g_tls=image.tlsAddress;
    if(!g_image||!InImage(g_entry))return false;
    const uint32_t encoded=*reinterpret_cast<uint32_t*>(g_image+0x154);
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

extern "C" LONG __stdcall K_RtlEnterCriticalSectionAndRegion(RTL_CRITICAL_SECTION* c) {
    K_KeEnterCriticalRegion();
    const LONG status = K_RtlEnterCriticalSection(c);
    if (status < 0) K_KeLeaveCriticalRegion();
    return status;
}
extern "C" void __stdcall K_RtlLeaveCriticalSectionAndRegion(RTL_CRITICAL_SECTION* c) {
    K_RtlLeaveCriticalSection(c);
    K_KeLeaveCriticalRegion();
}
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
static LONG RtlCaseUnicodeString(GuestUnicodeString* destination, const GuestUnicodeString* source, BOOL allocate, bool upper) {
    if (!destination || !source || (!source->Buffer && source->Length) || (source->Length & 1))
        return LONG(0xC000000Du);
    const ULONG bytes = source->Length;
    if (allocate) {
        if (bytes > 0xFFFD) return LONG(0xC0000023u);
        wchar_t* buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(wchar_t)));
        if (!buffer) return LONG(0xC0000017u);
        destination->Buffer = buffer;
        destination->MaximumLength = USHORT(bytes + sizeof(wchar_t));
    } else if (!destination->Buffer || destination->MaximumLength < bytes) {
        return LONG(0xC0000023u);
    }
    const ULONG chars = bytes / sizeof(wchar_t);
    for (ULONG i = 0; i < chars; ++i) {
        const wchar_t c = source->Buffer[i];
        destination->Buffer[i] = upper ? wchar_t(towupper(c)) : wchar_t(towlower(c));
    }
    if (destination->MaximumLength >= bytes + sizeof(wchar_t)) destination->Buffer[chars] = L'\\0';
    destination->Length = USHORT(bytes);
    return 0;
}
extern "C" LONG __stdcall K_RtlDowncaseUnicodeString(GuestUnicodeString* d, const GuestUnicodeString* s, BOOL allocate) {
    return RtlCaseUnicodeString(d, s, allocate, false);
}
extern "C" LONG __stdcall K_RtlUpcaseUnicodeString(GuestUnicodeString* d, const GuestUnicodeString* s, BOOL allocate) {
    return RtlCaseUnicodeString(d, s, allocate, true);
}
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
extern "C" LONG __stdcall K_NtAllocateVirtualMemory(void** base, ULONG_PTR, SIZE_T* size, ULONG type, ULONG protect) {
    if (!base || !size || !*size) return LONG(0xC000000Du);
    void* p = VirtualAlloc(*base, *size, type, protect);
    if (!p) return LONG(0xC0000017u);
    *base = p;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(p, &info, sizeof(info))) *size = info.RegionSize;
    return 0;
}
extern "C" LONG __stdcall K_NtProtectVirtualMemory(void** base,SIZE_T* size,ULONG protect,ULONG* oldProtect){
    if(!base||!*base||!size||!*size)return LONG(0xC000000Du);DWORD old=0;
    if(!VirtualProtect(*base,*size,protect,&old))return LONG(0xC000000Du);
    if(oldProtect)*oldProtect=old;return 0;
}
extern "C" LONG __stdcall K_NtQueryVirtualMemory(void* base,ULONG infoClass,void* out,ULONG length,ULONG* returned){
    if(infoClass!=0||!base||!out)return LONG(0xC0000003u);
    MEMORY_BASIC_INFORMATION info{};SIZE_T n=VirtualQuery(base,&info,sizeof(info));if(!n)return LONG(0xC000000Du);
    if(returned)*returned=ULONG(n);if(length<sizeof(info))return LONG(0xC0000023u);
    std::memcpy(out,&info,sizeof(info));return 0;
}
extern "C" LONG __stdcall K_NtCreateSemaphore(HANDLE* out,void*,LONG initial,LONG maximum){
    if(!out||maximum<=0||initial<0||initial>maximum)return LONG(0xC000000Du);
    HANDLE h=CreateSemaphoreW(nullptr,initial,maximum,nullptr);if(!h)return LONG(0xC0000017u);*out=h;return 0;
}
extern "C" LONG __stdcall K_NtReleaseSemaphore(HANDLE h,LONG count,LONG* previous){
    if(count<=0)return LONG(0xC000000Du);LONG p=0;BOOL ok=ReleaseSemaphore(h,count,&p);if(ok&&previous)*previous=p;return ok?0:LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtWaitForMultipleObjects(ULONG count,const HANDLE* handles,ULONG waitType,BOOL alertable,const LARGE_INTEGER* timeout,void*){
    if(!handles||!count||count>MAXIMUM_WAIT_OBJECTS||waitType>1)return LONG(0xC000000Du);
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=WaitForMultipleObjects(count,handles,waitType==0,ms);if(r>=WAIT_OBJECT_0&&r<WAIT_OBJECT_0+count)return 0;
    if(r>=WAIT_ABANDONED_0&&r<WAIT_ABANDONED_0+count)return LONG(0x80);
    return r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtSignalAndWaitForSingleObject(HANDLE signal,HANDLE wait,BOOL alertable,const LARGE_INTEGER* timeout){
    DWORD ms=INFINITE;if(timeout){LONGLONG t=timeout->QuadPart;if(t<0){ULONGLONG v=ULONGLONG(-t)/10000ull;ms=DWORD(v>MAXDWORD?MAXDWORD:v);}else ms=0;}
    DWORD r=SignalObjectAndWait(signal,wait,ms,FALSE);return r==WAIT_OBJECT_0?0:r==WAIT_TIMEOUT?LONG(0x102):LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtFreeVirtualMemory(void** base, SIZE_T* size, ULONG type) {
    if (!base || !*base) return LONG(0xC000000Du);
    SIZE_T n = size ? *size : 0;
    if (!VirtualFree(*base, n, type)) return LONG(0xC000000Du);
    if (type & MEM_RELEASE) { *base = nullptr; if (size) *size = 0; }
    return 0;
}

extern "C" void __stdcall K_KeInitializeEvent(void* event, ULONG type, BOOL initialState) {
    if (!event) return;
    HANDLE h = CreateEventW(nullptr, type == 0, initialState, nullptr);
    if (h) StoreKernelObject(event, h);
}
extern "C" LONG __stdcall K_KeSetEvent(void* event, LONG, BOOL) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    return SetEvent(h) ? previous : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KeResetEvent(void* event) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    return ResetEvent(h) ? previous : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KePulseEvent(void* event, LONG, BOOL) {
    HANDLE h = FindKernelObject(event);
    if (!h) return LONG(0xC0000008u);
    const LONG previous = WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    if (!PulseEvent(h)) return LONG(0xC0000008u);
    return previous;
}
extern "C" LONG __stdcall K_KeWaitForSingleObject(void* object, ULONG, ULONG, BOOL, const LARGE_INTEGER* timeout) {
    HANDLE h = FindKernelObject(object);
    if (!h) return LONG(0xC0000008u);
    const DWORD result = WaitForSingleObject(h, KernelTimeout(timeout));
    return result == WAIT_OBJECT_0 ? 0 : result == WAIT_TIMEOUT ? LONG(0x102) : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_KeWaitForMultipleObjects(ULONG count, void** objects, ULONG waitType, ULONG, ULONG,
                                                       BOOL, const LARGE_INTEGER* timeout, void*) {
    if (!objects || !count || count > MAXIMUM_WAIT_OBJECTS || waitType > 1) return LONG(0xC000000Du);
    HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
    for (ULONG i = 0; i < count; ++i) {
        handles[i] = FindKernelObject(objects[i]);
        if (!handles[i]) return LONG(0xC0000008u);
    }
    const DWORD result = WaitForMultipleObjects(count, handles, waitType == 0, KernelTimeout(timeout));
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) return LONG(result - WAIT_OBJECT_0);
    if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) return LONG(0x80);
    return result == WAIT_TIMEOUT ? LONG(0x102) : LONG(0xC0000008u);
}
extern "C" void __stdcall K_KeInitializeSemaphore(void* semaphore, LONG count, LONG limit) {
    if (!semaphore || limit <= 0 || count < 0 || count > limit) return;
    HANDLE h = CreateSemaphoreW(nullptr, count, limit, nullptr);
    if (h) StoreKernelObject(semaphore, h);
}
extern "C" LONG __stdcall K_KeReleaseSemaphore(void* semaphore, LONG, LONG adjustment, BOOL) {
    HANDLE h = FindKernelObject(semaphore);
    if (!h || adjustment <= 0) return LONG(0xC000000Du);
    LONG previous = 0;
    return ReleaseSemaphore(h, adjustment, &previous) ? previous : LONG(0xC0000008u);
}
extern "C" void __stdcall K_KeInitializeMutant(void* mutant, BOOL initialOwner) {
    if (!mutant) return;
    HANDLE h = CreateMutexW(nullptr, initialOwner, nullptr);
    if (h) StoreKernelObject(mutant, h);
}
extern "C" LONG __stdcall K_KeReleaseMutant(void* mutant, LONG, BOOL, BOOL) {
    HANDLE h = FindKernelObject(mutant);
    if (!h) return LONG(0xC0000008u);
    return ReleaseMutex(h) ? 0 : LONG(0xC0000008u);
}


extern "C" void __stdcall K_ExInitializeReadWriteLock(void* lock) { (void)GetGuestRwLock(lock, true); }
extern "C" void __stdcall K_ExAcquireReadWriteLockExclusive(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, true);
    if (native) { AcquireSRWLockExclusive(native); g_heldRwLocks[lock] = true; }
}
extern "C" void __stdcall K_ExAcquireReadWriteLockShared(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, true);
    if (native) { AcquireSRWLockShared(native); g_heldRwLocks[lock] = false; }
}
extern "C" void __stdcall K_ExReleaseReadWriteLock(void* lock) {
    SRWLOCK* native = GetGuestRwLock(lock, false);
    if (!native) return;
    auto it = g_heldRwLocks.find(lock);
    if (it == g_heldRwLocks.end()) return;
    const bool exclusive = it->second;
    g_heldRwLocks.erase(it);
    if (exclusive) ReleaseSRWLockExclusive(native);
    else ReleaseSRWLockShared(native);
}
extern "C" void __stdcall K_KeEnterCriticalRegion() { ++g_criticalRegionDepth; }
extern "C" void __stdcall K_KeLeaveCriticalRegion() { if (g_criticalRegionDepth) --g_criticalRegionDepth; }
extern "C" UCHAR __fastcall K_KfRaiseIrql(UCHAR level) {
    const UCHAR previous = UCHAR(g_guestIrql);
    if (level > g_guestIrql) g_guestIrql = level;
    return previous;
}
extern "C" void __fastcall K_KfLowerIrql(UCHAR level) { g_guestIrql = level; }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToDpcLevel() { return K_KfRaiseIrql(2); }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToSynchLevel() { return K_KfRaiseIrql(1); }
extern "C" void __stdcall K_KeInitializeTimerEx(void* timer, ULONG type) {
    if (!timer) return;
    HANDLE h = CreateWaitableTimerW(nullptr, type == 0, nullptr);
    if (h) StoreKernelObject(timer, h);
}
extern "C" BOOL __stdcall K_KeSetTimer(void* timer, const LARGE_INTEGER* dueTime, void*) {
    HANDLE h = FindKernelObject(timer);
    return h && dueTime ? SetWaitableTimer(h, dueTime, 0, nullptr, nullptr, FALSE) : FALSE;
}
extern "C" BOOL __stdcall K_KeSetTimerEx(void* timer, const LARGE_INTEGER* dueTime, LONG period, void*) {
    HANDLE h = FindKernelObject(timer);
    return h && dueTime && period >= 0 ? SetWaitableTimer(h, dueTime, period, nullptr, nullptr, FALSE) : FALSE;
}
extern "C" BOOL __stdcall K_KeCancelTimer(void* timer) {
    HANDLE h = FindKernelObject(timer);
    return h ? CancelWaitableTimer(h) : FALSE;
}
extern "C" uint64_t __stdcall K_KeQueryInterruptTime() {
    return GetTickCount64() * 10000ull;
}
extern "C" void __stdcall K_KeStallExecutionProcessor(uint32_t microseconds) {
    LARGE_INTEGER start{}, now{}, frequency{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const uint64_t target = (uint64_t(frequency.QuadPart) * microseconds) / 1000000ull;
    do { QueryPerformanceCounter(&now); YieldProcessor(); }
    while (uint64_t(now.QuadPart - start.QuadPart) < target);
}
extern "C" LONG __stdcall K_NtYieldExecution() {
    SwitchToThread();
    return 0;
}
extern "C" uint32_t __stdcall K_MmQueryAddressProtect(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) ? info.Protect : 0;
}
extern "C" uint32_t __stdcall K_MmQueryAllocationSize(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) ? uint32_t(info.RegionSize) : 0;
}
extern "C" void __stdcall K_MmSetAddressProtect(void* address, uint32_t size, uint32_t protect) {
    DWORD oldProtect = 0;
    if (address && size) VirtualProtect(address, size, protect, &oldProtect);
}
struct GuestTimeFields { SHORT Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday; };
extern "C" BOOL __stdcall K_RtlTimeFieldsToTime(const GuestTimeFields* fields, LARGE_INTEGER* time) {
    if (!fields || !time) return FALSE;
    SYSTEMTIME st{};
    st.wYear = WORD(fields->Year); st.wMonth = WORD(fields->Month); st.wDay = WORD(fields->Day);
    st.wHour = WORD(fields->Hour); st.wMinute = WORD(fields->Minute); st.wSecond = WORD(fields->Second);
    st.wMilliseconds = WORD(fields->Milliseconds);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return FALSE;
    ULARGE_INTEGER value{}; value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime;
    time->QuadPart = LONGLONG(value.QuadPart);
    return TRUE;
}
extern "C" void __stdcall K_RtlTimeToTimeFields(const LARGE_INTEGER* time, GuestTimeFields* fields) {
    if (!time || !fields) return;
    ULARGE_INTEGER value{}; value.QuadPart = ULONGLONG(time->QuadPart);
    FILETIME ft{}; ft.dwLowDateTime = value.LowPart; ft.dwHighDateTime = value.HighPart;
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) { std::memset(fields, 0, sizeof(*fields)); return; }
    fields->Year = SHORT(st.wYear); fields->Month = SHORT(st.wMonth); fields->Day = SHORT(st.wDay);
    fields->Hour = SHORT(st.wHour); fields->Minute = SHORT(st.wMinute); fields->Second = SHORT(st.wSecond);
    fields->Milliseconds = SHORT(st.wMilliseconds); fields->Weekday = SHORT(st.wDayOfWeek);
}
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
        cmp eax, 45
        je smbus_read
        cmp eax, 50
        je smbus_write
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
        cmp eax, 12
        je ex_acquire_rw_exclusive
        cmp eax, 13
        je ex_acquire_rw_shared
        cmp eax, 18
        je ex_init_rw
        cmp eax, 28
        je ex_release_rw
                cmp eax, 97
        je ke_cancel_timer
        cmp eax, 101
        je ke_enter_critical
        cmp eax, 113
        je ke_init_timer
        cmp eax, 122
        je ke_leave_critical
        cmp eax, 129
        je ke_raise_dpc
        cmp eax, 130
        je ke_raise_synch
        cmp eax, 149
        je ke_set_timer
        cmp eax, 150
        je ke_set_timer_ex
        cmp eax, 160
        je kf_raise_irql
        cmp eax, 161
        je kf_lower_irql
                cmp eax, 108
        je ke_init_event
        cmp eax, 110
        je ke_init_mutant
        cmp eax, 112
        je ke_init_semaphore
        cmp eax, 123
        je ke_pulse_event
        cmp eax, 132
        je ke_release_semaphore
        cmp eax, 131
        je ke_release_mutant
        cmp eax, 138
        je ke_reset_event
        cmp eax, 145
        je ke_set_event
        cmp eax, 158
        je ke_wait_multiple
        cmp eax, 159
        je ke_wait_single
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
        cmp eax, 125
        je query_interrupt
        cmp eax, 151
        je stall_processor
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
        cmp eax, 179
        je mm_query_protect
        cmp eax, 180
        je mm_query_size
        cmp eax, 182
        je mm_set_protect
        cmp eax, 184
        je nt_alloc_vm
        cmp eax, 199
        je nt_free_vm
        cmp eax, 238
        je nt_yield
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
        cmp eax, 268
        je rtl_cmp_memory
        cmp eax, 270
        je rtl_cmp_a
        cmp eax, 271
        je rtl_cmp_w
        cmp eax, 272
        je rtl_copy_a
        cmp eax, 274
        je rtl_create_unicode
        cmp eax, 273
        je rtl_copy_w
        cmp eax, 276
        je rtl_downcase_unicode_string
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
        cmp eax, 314
        je rtl_upcase_unicode_string
        cmp eax, 315
        je rtl_upcase_unicode_mb
        cmp eax, 317
        je rtl_upper_string
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
        cmp eax, 267
        je rtl_char_to_integer
        cmp eax, 292
        je rtl_integer_to_char
        cmp eax, 293
        je rtl_integer_to_unicode
        cmp eax, 300
        je rtl_mb_to_unicode_size
        cmp eax, 299
        je rtl_mb_to_unicode
        cmp eax, 309
        je rtl_unicode_to_integer
        cmp eax, 311
        je rtl_unicode_to_mb_size
        cmp eax, 310
        je rtl_unicode_to_mb
        cmp eax, 260
        je rtl_ansi_to_unicode
        cmp eax, 269
        je rtl_cmp_mem_ulong
        cmp eax, 285
        je rtl_fill_ulong
        cmp eax, 286
        je rtl_free_ansi
        cmp eax, 287
        je rtl_free_unicode
        cmp eax, 297
        je rtl_map_generic
        cmp eax, 301
        je rtl_status_dos
        cmp eax, 304
        je rtl_time_fields_to_time
        cmp eax, 305
        je rtl_time_to_fields
        cmp eax, 308
        je rtl_unicode_to_ansi
        cmp eax, 204
        je nt_protect_vm
        cmp eax, 217
        je nt_query_vm
        cmp eax, 193
        je nt_create_semaphore
        cmp eax, 222
        je nt_release_semaphore
        cmp eax, 235
        je nt_wait_multiple
        cmp eax, 230
        je nt_signal_wait
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
    smbus_read: jmp K_HalReadSMBusValue
    smbus_write: jmp K_HalWriteSMBusValue
    icx: jmp K_InterlockedCompareExchange
    idec: jmp K_InterlockedDecrement
    iinc: jmp K_InterlockedIncrement
    iex: jmp K_InterlockedExchange
    iexa: jmp K_InterlockedExchangeAdd
    ex_acquire_rw_exclusive: jmp K_ExAcquireReadWriteLockExclusive
    ex_acquire_rw_shared: jmp K_ExAcquireReadWriteLockShared
    ex_init_rw: jmp K_ExInitializeReadWriteLock
    ex_release_rw: jmp K_ExReleaseReadWriteLock
    ke_cancel_timer: jmp K_KeCancelTimer
    ke_enter_critical: jmp K_KeEnterCriticalRegion
    ke_init_timer: jmp K_KeInitializeTimerEx
    ke_leave_critical: jmp K_KeLeaveCriticalRegion
    ke_raise_dpc: jmp K_KeRaiseIrqlToDpcLevel
    ke_raise_synch: jmp K_KeRaiseIrqlToSynchLevel
    ke_set_timer: jmp K_KeSetTimer
    ke_set_timer_ex: jmp K_KeSetTimerEx
    kf_raise_irql: jmp K_KfRaiseIrql
    kf_lower_irql: jmp K_KfLowerIrql
    ke_init_event: jmp K_KeInitializeEvent
    ke_init_mutant: jmp K_KeInitializeMutant
    ke_init_semaphore: jmp K_KeInitializeSemaphore
    ke_pulse_event: jmp K_KePulseEvent
    ke_release_semaphore: jmp K_KeReleaseSemaphore
    ke_release_mutant: jmp K_KeReleaseMutant
    ke_reset_event: jmp K_KeResetEvent
    ke_set_event: jmp K_KeSetEvent
    ke_wait_multiple: jmp K_KeWaitForMultipleObjects
    ke_wait_single: jmp K_KeWaitForSingleObject
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
    query_interrupt: jmp K_KeQueryInterruptTime
    stall_processor: jmp K_KeStallExecutionProcessor
    mm_query_protect: jmp K_MmQueryAddressProtect
    mm_query_size: jmp K_MmQueryAllocationSize
    mm_set_protect: jmp K_MmSetAddressProtect
    nt_alloc_vm: jmp K_NtAllocateVirtualMemory
    nt_free_vm: jmp K_NtFreeVirtualMemory
    nt_yield: jmp K_NtYieldExecution
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
    rtl_cmp_memory: jmp K_RtlCompareMemory
    rtl_create_unicode: jmp K_RtlCreateUnicodeString
    rtl_upcase_unicode_mb: jmp K_RtlUpcaseUnicodeToMultiByteN
    rtl_upper_string: jmp K_RtlUpperString
    rtl_cmp_w: jmp K_RtlCompareUnicodeString
    rtl_copy_a: jmp K_RtlCopyString
    rtl_copy_w: jmp K_RtlCopyUnicodeString
    rtl_lower_w: jmp K_RtlDowncaseUnicodeChar
    rtl_downcase_unicode_string: jmp K_RtlDowncaseUnicodeString
    rtl_upcase_unicode_string: jmp K_RtlUpcaseUnicodeString
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

    rtl_char_to_integer: jmp K_RtlCharToInteger
    rtl_integer_to_char: jmp K_RtlIntegerToChar
    rtl_integer_to_unicode: jmp K_RtlIntegerToUnicodeString
    rtl_mb_to_unicode: jmp K_RtlMultiByteToUnicodeN
    rtl_unicode_to_integer: jmp K_RtlUnicodeStringToInteger
    rtl_unicode_to_mb: jmp K_RtlUnicodeToMultiByteN
    rtl_mb_to_unicode_size: jmp K_RtlMultiByteToUnicodeSize
    rtl_unicode_to_mb_size: jmp K_RtlUnicodeToMultiByteSize
    rtl_ansi_to_unicode: jmp K_RtlAnsiStringToUnicodeString
    rtl_cmp_mem_ulong: jmp K_RtlCompareMemoryUlong
    rtl_fill_ulong: jmp K_RtlFillMemoryUlong
    rtl_free_ansi: jmp K_RtlFreeAnsiString
    rtl_free_unicode: jmp K_RtlFreeUnicodeString
    rtl_map_generic: jmp K_RtlMapGenericMask
    rtl_status_dos: jmp K_RtlNtStatusToDosError
    rtl_time_fields_to_time: jmp K_RtlTimeFieldsToTime
    rtl_time_to_fields: jmp K_RtlTimeToTimeFields
    rtl_unicode_to_ansi: jmp K_RtlUnicodeStringToAnsiString
    nt_protect_vm: jmp K_NtProtectVirtualMemory
    nt_query_vm: jmp K_NtQueryVirtualMemory
    nt_create_semaphore: jmp K_NtCreateSemaphore
    nt_release_semaphore: jmp K_NtReleaseSemaphore
    nt_wait_multiple: jmp K_NtWaitForMultipleObjects
    nt_signal_wait: jmp K_NtSignalAndWaitForSingleObject
    }
}
namespace {
bool DecodeVa(uint32_t encoded,uint32_t key,uint32_t&va){va=encoded^key;return InImage(va,4);}
}
bool XboxKernelInitializeEeprom(const wchar_t* path) {
    if (!path || !*path) return false;
    g_eepromPath = path;
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) != FALSE;
    if (ok && size.QuadPart == 0) {
        // Blank EEPROM is intentionally unprovisioned, not a fabricated Xbox identity.
        g_eeprom.fill(0xFF);
        DWORD written = 0;
        ok = WriteFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &written, nullptr) &&
             written == g_eeprom.size() && FlushFileBuffers(f);
    } else if (ok && size.QuadPart == static_cast<LONGLONG>(g_eeprom.size())) {
        DWORD read = 0;
        ok = SetFilePointer(f, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
             ReadFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &read, nullptr) &&
             read == g_eeprom.size();
    } else {
        ok = false; // Never silently truncate or replace a user's EEPROM backup.
    }
    CloseHandle(f);
    g_eepromReady = ok;
    if (ok) OutputDebugStringW(L"[XboxEEPROM] Persistent 256-byte EEPROM loaded. A blank all-FF image is unprovisioned and cannot register with Insignia.\\n");
    return ok;
}

void XboxKernelFlushEeprom() {
    if (!g_eepromReady || g_eepromPath.empty()) return;
    std::lock_guard<std::mutex> lock(g_eepromMutex);
    HANDLE f = CreateFileW(g_eepromPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &written, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
}

void XboxKernelSetTitlePath(const wchar_t* path){
    g_titleDirectory.clear();if(!path)return;g_titleDirectory=path;size_t p=g_titleDirectory.find_last_of(L"\\/");if(p!=std::wstring::npos)g_titleDirectory.resize(p);
}
bool XboxKernelResolveImports(const XboxKernelState& image){
    g_image=reinterpret_cast<uint8_t*>(uintptr_t(image.imageBase));g_base=image.imageBase;g_size=image.imageSize;g_entry=image.entryPoint;g_tls=image.tlsAddress;
    if(!g_image||!InImage(g_entry))return false;
    const uint32_t encoded=*reinterpret_cast<uint32_t*>(g_image+0x154);
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
