#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <cstdarg>
#include <intrin.h>
#include <cstring>
#include <cwchar>
#include <cctype>
#include <cwctype>
#include <string>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <chrono>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <utility>
#include <memory>
#include <atomic>
#include "kernel_compat.h"
#include "xdvdfs.h"

extern "C" void KernelDispatch();
namespace {
constexpr uint32_t kRetailThunk=0x5B6D40B6u, kDebugThunk=0xEFB1F152u;
constexpr uint32_t kThunkMax=379;
static uint8_t* g_image=nullptr;
static uint32_t g_base=0,g_size=0,g_entry=0,g_tls=0;
static void* g_stubPage=nullptr;
static uint32_t g_stubCount=0;
static void* g_savedAv=nullptr;
static std::array<std::array<uint8_t, 512>, 379> g_data{};
static std::wstring g_titleDirectory;
ULONG g_fileCacheSize = 1024 * 1024;
std::array<uint8_t, 256> g_eeprom{};
std::wstring g_eepromPath;
std::mutex g_eepromMutex;
std::mutex g_fileIoMutex;
XDVDFS_VOLUME_INFO g_dvdInfo{};
bool g_dvdReady = false;
struct GuestDvdFile {
    std::string path;
    uint32_t size = 0;
    uint32_t startSector = 0;
    uint8_t attributes = 0;
    uint64_t position = 0;
    bool directory = false;
    uint32_t directoryIndex = 0;
    std::vector<XDVDFS_FILE_INFO> children;
};
std::mutex g_dvdFileMutex;
std::unordered_map<HANDLE, GuestDvdFile> g_dvdFiles;
std::mutex g_stringAllocationMutex;
std::unordered_set<void*> g_stringAllocations;
std::mutex g_kernelObjectMutex;
std::unordered_map<void*, HANDLE> g_kernelObjects;
struct GuestDirectorySearch {
    HANDLE search = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW current{};
    bool currentValid = false;
};
std::mutex g_directorySearchMutex;
std::unordered_map<HANDLE, GuestDirectorySearch> g_directorySearches;
static void CloseGuestDirectorySearch(HANDLE file) {
    std::lock_guard<std::mutex> lock(g_directorySearchMutex);
    auto it = g_directorySearches.find(file);
    if (it != g_directorySearches.end()) {
        if (it->second.search != INVALID_HANDLE_VALUE) FindClose(it->second.search);
        g_directorySearches.erase(it);
    }
}
std::mutex g_rwLockMapMutex;
std::unordered_map<void*, std::unique_ptr<SRWLOCK>> g_rwLocks;
thread_local std::unordered_map<void*, bool> g_heldRwLocks;
SRWLOCK* GetGuestRwLock(void* address, bool create) {
    if (!address) return nullptr;
    std::lock_guard<std::mutex> lock(g_rwLockMapMutex);
    auto it = g_rwLocks.find(address);
    if (it != g_rwLocks.end()) return it->second.get();
    if (!create) return nullptr;
    auto value = std::make_unique<SRWLOCK>();
    InitializeSRWLock(value.get());
    SRWLOCK* result = value.get();
    g_rwLocks.emplace(address, std::move(value));
    return result;
}
thread_local ULONG g_guestIrql = 0;
thread_local bool g_insideDpc = false;
struct GuestDpcInfo {
    using Routine = void (__stdcall*)(void*, void*, void*, void*);
    Routine routine = nullptr;
    void* context = nullptr;
    std::atomic<bool> pending{false};
};
struct GuestDpcWork {
    GuestDpcInfo* info;
    void* dpc;
    void* arg1;
    void* arg2;
};
std::mutex g_dpcMutex;
std::unordered_map<void*, std::unique_ptr<GuestDpcInfo>> g_dpcs;
DWORD WINAPI GuestDpcWorker(void* raw) {
    std::unique_ptr<GuestDpcWork> work(static_cast<GuestDpcWork*>(raw));
    GuestDpcInfo* info = work->info;
    if (info && info->pending.exchange(false)) {
        g_insideDpc = true;
        info->routine(work->dpc, info->context, work->arg1, work->arg2);
        g_insideDpc = false;
    }
    return 0;
}
thread_local ULONG g_criticalRegionDepth = 0;
HANDLE FindKernelObject(void* object) {
    std::lock_guard<std::mutex> lock(g_kernelObjectMutex);
    auto it = g_kernelObjects.find(object);
    return it == g_kernelObjects.end() ? nullptr : it->second;
}
void StoreKernelObject(void* object, HANDLE handle) {
    if (!object || !handle) return;
    std::lock_guard<std::mutex> lock(g_kernelObjectMutex);
    auto it = g_kernelObjects.find(object);
    if (it != g_kernelObjects.end()) CloseHandle(it->second);
    g_kernelObjects[object] = handle;
}
DWORD KernelTimeout(const LARGE_INTEGER* timeout) {
    if (!timeout) return INFINITE;
    const LONGLONG ticks = timeout->QuadPart;
    ULONGLONG remaining100ns = 0;
    if (ticks < 0) {
        // Relative NT intervals are negative 100-ns units.
        remaining100ns = ULONGLONG(-(ticks + 1)) + 1;
    } else if (ticks > 0) {
        // Absolute NT time is measured from 1601-01-01, like FILETIME.
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER now{};
        now.LowPart = ft.dwLowDateTime;
        now.HighPart = ft.dwHighDateTime;
        const ULONGLONG deadline = static_cast<ULONGLONG>(ticks);
        if (deadline <= now.QuadPart) return 0;
        remaining100ns = deadline - now.QuadPart;
    }
    if (!remaining100ns) return 0;
    // Round up: a positive sub-millisecond interval must not become a poll.
    const ULONGLONG ms = (remaining100ns + 9999ull) / 10000ull;
    return DWORD(ms > MAXDWORD - 1ull ? MAXDWORD - 1ull : ms);
}
void TrackStringAllocation(void* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lock(g_stringAllocationMutex);
    g_stringAllocations.insert(p);
}
bool FreeStringAllocation(void* p) {
    if (!p) return false;
    std::lock_guard<std::mutex> lock(g_stringAllocationMutex);
    auto it = g_stringAllocations.find(p);
    if (it == g_stringAllocations.end()) return false;
    g_stringAllocations.erase(it);
    HeapFree(GetProcessHeap(), 0, p);
    return true;
}
bool g_eepromReady = false;

constexpr uint32_t kStatusInvalidParameter = 0xC000000Du;
constexpr uint32_t kStatusDeviceNotConnected = 0xC000009Du;

extern "C" uint32_t __stdcall K_HalReadSMBusValue(uint8_t address, uint8_t command, uint8_t word, uint32_t* value) {
    if (!value) return kStatusInvalidParameter;
    if (address != 0xA8 || !g_eepromReady) return kStatusDeviceNotConnected;
    const uint32_t count = word ? 2u : 1u;
    if (uint32_t(command) + count > g_eeprom.size()) return kStatusInvalidParameter;
    std::lock_guard<std::mutex> lock(g_eepromMutex);
    *value = g_eeprom[command];
    if (word) *value |= uint32_t(g_eeprom[uint32_t(command) + 1]) << 8;
    return 0;
}

extern "C" uint32_t __stdcall K_HalWriteSMBusValue(uint8_t address, uint8_t command, uint8_t word, uint32_t value) {
    if (address != 0xA8 || !g_eepromReady) return kStatusDeviceNotConnected;
    const uint32_t count = word ? 2u : 1u;
    if (uint32_t(command) + count > g_eeprom.size()) return kStatusInvalidParameter;
    std::lock_guard<std::mutex> lock(g_eepromMutex);
    g_eeprom[command] = uint8_t(value & 0xFFu);
    if (word) g_eeprom[uint32_t(command) + 1] = uint8_t((value >> 8) & 0xFFu);
    // Persist each SMBus write so a guest crash does not discard EEPROM changes.
    HANDLE f = CreateFileW(g_eepromPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return kStatusDeviceNotConnected;
    DWORD written = 0;
    BOOL ok = WriteFile(f, g_eeprom.data(), DWORD(g_eeprom.size()), &written, nullptr);
    if (ok) ok = (written == DWORD(g_eeprom.size())) && FlushFileBuffers(f);
    CloseHandle(f);
    return ok ? 0u : kStatusDeviceNotConnected;
}

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
    case 16: case 22: case 30: case 31: case 40: case 41: case 42:
    case 64: case 70: case 71: case 88: case 89: case 102: case 120:
    case 154: case 156: case 157: case 162: case 164: case 240: case 245:
    case 249: case 259: case 321: case 322: case 323: case 324: case 325:
    case 326: case 353: case 354: case 355: case 356: case 357:
        return g_data[ordinal].data();
    default:return nullptr;
    }
}
extern "C" void* __stdcall K_ExAllocatePool(uint32_t n){return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,n?n:1);}
extern "C" void* __stdcall K_ExAllocatePoolWithTag(uint32_t n,uint32_t){return K_ExAllocatePool(n);}
extern "C" void __stdcall K_ExFreePool(void*p){if(p)HeapFree(GetProcessHeap(),0,p);}



extern "C" uint32_t __stdcall K_ExQueryPoolBlockSize(void*p){return p?HeapSize(GetProcessHeap(),0,p):0xFFFFFFFFu;}
extern "C" void* __stdcall K_AvGetSavedDataAddress(){return g_savedAv;}
extern "C" void __stdcall K_AvSetSavedDataAddress(void*p){g_savedAv=p;}

struct GuestListEntry { GuestListEntry* Flink; GuestListEntry* Blink; };

struct GuestDispatcherHeader {
    UCHAR Type, Absolute, Size, Inserted;
    LONG SignalState;
    GuestListEntry WaitListHead;
};
struct GuestKQueue {
    GuestDispatcherHeader Header;
    GuestListEntry EntryListHead;
    ULONG CurrentCount;
    ULONG MaximumCount;
    GuestListEntry ThreadListHead;
};
struct GuestQueueState {
    std::mutex mutex;
    std::condition_variable changed;
};
static std::mutex g_guestQueueMapMutex;
static std::unordered_map<void*, std::shared_ptr<GuestQueueState>> g_guestQueues;
static thread_local std::unordered_set<void*> g_activeGuestQueues;
static void InitializeGuestList(GuestListEntry* head) {
    head->Flink = head;
    head->Blink = head;
}
static std::shared_ptr<GuestQueueState> GetGuestQueueState(GuestKQueue* queue, bool create) {
    if (!queue) return {};
    std::lock_guard<std::mutex> lock(g_guestQueueMapMutex);
    auto it = g_guestQueues.find(queue);
    if (it != g_guestQueues.end()) return it->second;
    if (!create) return {};
    auto state = std::make_shared<GuestQueueState>();
    g_guestQueues.emplace(queue, state);
    return state;
}
extern "C" void __stdcall K_KeInitializeQueue(GuestKQueue* queue, ULONG count) {
    if (!queue) return;
    auto state = GetGuestQueueState(queue, true);
    std::lock_guard<std::mutex> lock(state->mutex);
    queue->Header.Type = 4; // QueueObject
    queue->Header.Absolute = 0;
    queue->Header.Size = static_cast<UCHAR>(sizeof(GuestKQueue) / sizeof(ULONG));
    queue->Header.Inserted = 0;
    queue->Header.SignalState = 0;
    InitializeGuestList(&queue->Header.WaitListHead);
    InitializeGuestList(&queue->EntryListHead);
    queue->CurrentCount = 0;
    queue->MaximumCount = count > 1 ? count : 1;
    InitializeGuestList(&queue->ThreadListHead);
    g_activeGuestQueues.erase(queue);
}
static LONG InsertGuestQueueEntry(GuestKQueue* queue, GuestListEntry* entry, bool atHead) {
    if (!queue || !entry) return 0;
    auto state = GetGuestQueueState(queue, true);
    std::lock_guard<std::mutex> lock(state->mutex);
    GuestListEntry* head = &queue->EntryListHead;
    const LONG previous = queue->Header.SignalState;
    if (atHead) {
        entry->Flink = head->Flink;
        entry->Blink = head;
        head->Flink->Blink = entry;
        head->Flink = entry;
    } else {
        entry->Flink = head;
        entry->Blink = head->Blink;
        head->Blink->Flink = entry;
        head->Blink = entry;
    }
    if (queue->Header.SignalState < LONG(0x7FFFFFFF)) ++queue->Header.SignalState;
    state->changed.notify_one();
    return previous;
}
extern "C" LONG __stdcall K_KeInsertQueue(GuestKQueue* queue, GuestListEntry* entry) {
    return InsertGuestQueueEntry(queue, entry, false);
}
extern "C" LONG __stdcall K_KeInsertHeadQueue(GuestKQueue* queue, GuestListEntry* entry) {
    return InsertGuestQueueEntry(queue, entry, true);
}
extern "C" GuestListEntry* __stdcall K_KeRemoveQueue(
    GuestKQueue* queue, ULONG, const LARGE_INTEGER* timeout) {
    if (!queue) return reinterpret_cast<GuestListEntry*>(uintptr_t(0xC000000Du));
    auto state = GetGuestQueueState(queue, true);
    std::unique_lock<std::mutex> lock(state->mutex);
    if (g_activeGuestQueues.erase(queue)) {
        if (queue->CurrentCount) --queue->CurrentCount;
        state->changed.notify_all();
    }
    auto available = [&]() {
        return queue->Header.SignalState > 0 &&
               queue->EntryListHead.Flink != &queue->EntryListHead &&
               queue->CurrentCount < queue->MaximumCount;
    };
    if (!available()) {
        if (!timeout) {
            state->changed.wait(lock, available);
        } else {
            const DWORD milliseconds = KernelTimeout(timeout);
            if (!milliseconds || !state->changed.wait_for(
                    lock, std::chrono::milliseconds(milliseconds), available))
                return reinterpret_cast<GuestListEntry*>(uintptr_t(0x00000102u)); // STATUS_TIMEOUT
        }
    }
    if (!available())
        return reinterpret_cast<GuestListEntry*>(uintptr_t(0xC0000001u));
    GuestListEntry* head = &queue->EntryListHead;
    GuestListEntry* entry = head->Flink;
    GuestListEntry* next = entry->Flink;
    head->Flink = next;
    next->Blink = head;
    entry->Flink = nullptr;
    entry->Blink = nullptr;
    --queue->Header.SignalState;
    ++queue->CurrentCount;
    g_activeGuestQueues.insert(queue);
    return entry;
}

static void GuestAcquireSpinLock(volatile LONG* lock) {
    while (InterlockedExchange(lock, 1) != 0) YieldProcessor();
}
static void GuestReleaseSpinLock(volatile LONG* lock) {
    MemoryBarrier();
    InterlockedExchange(lock, 0);
}

extern "C" LARGE_INTEGER __stdcall K_ExInterlockedAddLargeInteger(
    LARGE_INTEGER* addend, LARGE_INTEGER increment, volatile LONG* lock) {
    LARGE_INTEGER previous{};
    if (!addend || !lock) return previous;
    GuestAcquireSpinLock(lock);
    previous = *addend;
    addend->QuadPart += increment.QuadPart;
    GuestReleaseSpinLock(lock);
    return previous;
}

extern "C" void __fastcall K_ExInterlockedAddLargeStatistic(
    LARGE_INTEGER* addend, ULONG increment) {
    if (!addend) return;
    volatile LONGLONG* target = &addend->QuadPart;
    LONGLONG observed = *target;
    for (;;) {
        const LONGLONG desired = observed + static_cast<LONGLONG>(increment);
        const LONGLONG actual = InterlockedCompareExchange64(target, desired, observed);
        if (actual == observed) return;
        observed = actual;
    }
}

extern "C" LONGLONG __fastcall K_ExInterlockedCompareExchange64(
    volatile LONGLONG* destination, const LONGLONG* exchange, const LONGLONG* comparand) {
    if (!destination || !exchange || !comparand) return 0;
    return InterlockedCompareExchange64(destination, *exchange, *comparand);
}

// Xbox KDEVICE_QUEUE/KDEVICE_QUEUE_ENTRY are 32-bit guest structures.
// One host mutex serializes the queue family, including RemoveEntry (which
// receives only an entry pointer and therefore cannot locate its queue lock).
struct GuestDeviceQueue {
    SHORT Type;
    SHORT Size;
    GuestListEntry DeviceListHead;
    volatile LONG Lock;
    UCHAR Busy;
    UCHAR Reserved[3];
};
struct GuestDeviceQueueEntry {
    GuestListEntry DeviceListEntry;
    ULONG SortKey;
    UCHAR Inserted;
    UCHAR Reserved[3];
};
static std::mutex g_deviceQueueMutex;

extern "C" void __stdcall K_KeInitializeDeviceQueue(GuestDeviceQueue* queue) {
    if (!queue) return;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    queue->Type = 4;
    queue->Size = sizeof(*queue);
    queue->DeviceListHead.Flink = &queue->DeviceListHead;
    queue->DeviceListHead.Blink = &queue->DeviceListHead;
    queue->Lock = 0;
    queue->Busy = FALSE;
    std::memset(queue->Reserved, 0, sizeof(queue->Reserved));
}
extern "C" BOOL __stdcall K_KeInsertDeviceQueue(GuestDeviceQueue* queue, GuestDeviceQueueEntry* entry) {
    if (!queue || !entry) return FALSE;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    if (entry->Inserted) return FALSE;
    if (!queue->Busy) {
        queue->Busy = TRUE;
        return FALSE; // caller owns the device; entry was not queued
    }
    GuestListEntry* tail = queue->DeviceListHead.Blink;
    entry->DeviceListEntry.Flink = &queue->DeviceListHead;
    entry->DeviceListEntry.Blink = tail;
    tail->Flink = &entry->DeviceListEntry;
    queue->DeviceListHead.Blink = &entry->DeviceListEntry;
    entry->Inserted = TRUE;
    return TRUE;
}
extern "C" BOOL __stdcall K_KeInsertByKeyDeviceQueue(GuestDeviceQueue* queue, GuestDeviceQueueEntry* entry, ULONG key) {
    if (!queue || !entry) return FALSE;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    if (entry->Inserted) return FALSE;
    if (!queue->Busy) {
        queue->Busy = TRUE;
        return FALSE;
    }
    entry->SortKey = key;
    GuestListEntry* cursor = queue->DeviceListHead.Flink;
    while (cursor != &queue->DeviceListHead) {
        auto* current = reinterpret_cast<GuestDeviceQueueEntry*>(cursor);
        if (key < current->SortKey) break;
        cursor = cursor->Flink;
    }
    entry->DeviceListEntry.Flink = cursor;
    entry->DeviceListEntry.Blink = cursor->Blink;
    cursor->Blink->Flink = &entry->DeviceListEntry;
    cursor->Blink = &entry->DeviceListEntry;
    entry->Inserted = TRUE;
    return TRUE;
}
extern "C" GuestDeviceQueueEntry* __stdcall K_KeRemoveDeviceQueue(GuestDeviceQueue* queue) {
    if (!queue) return nullptr;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    GuestListEntry* first = queue->DeviceListHead.Flink;
    if (first == &queue->DeviceListHead) {
        queue->Busy = FALSE;
        return nullptr;
    }
    first->Flink->Blink = &queue->DeviceListHead;
    queue->DeviceListHead.Flink = first->Flink;
    auto* entry = reinterpret_cast<GuestDeviceQueueEntry*>(first);
    entry->DeviceListEntry.Flink = nullptr;
    entry->DeviceListEntry.Blink = nullptr;
    entry->Inserted = FALSE;
    return entry;
}
extern "C" GuestDeviceQueueEntry* __stdcall K_KeRemoveByKeyDeviceQueue(GuestDeviceQueue* queue, ULONG key) {
    if (!queue) return nullptr;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    GuestListEntry* head = &queue->DeviceListHead;
    GuestListEntry* selected = nullptr;
    for (GuestListEntry* p = head->Flink; p != head; p = p->Flink) {
        auto* entry = reinterpret_cast<GuestDeviceQueueEntry*>(p);
        if (entry->SortKey >= key) { selected = p; break; }
    }
    if (!selected) selected = head->Flink;
    if (selected == head) {
        queue->Busy = FALSE;
        return nullptr;
    }
    selected->Blink->Flink = selected->Flink;
    selected->Flink->Blink = selected->Blink;
    auto* entry = reinterpret_cast<GuestDeviceQueueEntry*>(selected);
    entry->DeviceListEntry.Flink = nullptr;
    entry->DeviceListEntry.Blink = nullptr;
    entry->Inserted = FALSE;
    return entry;
}
extern "C" BOOL __stdcall K_KeRemoveEntryDeviceQueue(GuestDeviceQueueEntry* entry) {
    if (!entry) return FALSE;
    std::lock_guard<std::mutex> guard(g_deviceQueueMutex);
    if (!entry->Inserted || !entry->DeviceListEntry.Flink || !entry->DeviceListEntry.Blink) return FALSE;
    entry->DeviceListEntry.Blink->Flink = entry->DeviceListEntry.Flink;
    entry->DeviceListEntry.Flink->Blink = entry->DeviceListEntry.Blink;
    entry->DeviceListEntry.Flink = nullptr;
    entry->DeviceListEntry.Blink = nullptr;
    entry->Inserted = FALSE;
    return TRUE;
}
extern "C" GuestListEntry* __stdcall K_ExfInterlockedInsertHeadList(
    GuestListEntry* head, GuestListEntry* entry, volatile LONG* lock) {
    if (!head || !entry || !lock || entry == head) return nullptr;
    GuestAcquireSpinLock(lock);
    GuestListEntry* old = head->Flink;
    if (!old) { GuestReleaseSpinLock(lock); return nullptr; }
    entry->Flink = old;
    entry->Blink = head;
    old->Blink = entry;
    head->Flink = entry;
    GuestReleaseSpinLock(lock);
    return old == head ? nullptr : old;
}
extern "C" GuestListEntry* __stdcall K_ExfInterlockedInsertTailList(
    GuestListEntry* head, GuestListEntry* entry, volatile LONG* lock) {
    if (!head || !entry || !lock || entry == head) return nullptr;
    GuestAcquireSpinLock(lock);
    GuestListEntry* old = head->Blink;
    if (!old) { GuestReleaseSpinLock(lock); return nullptr; }
    entry->Flink = head;
    entry->Blink = old;
    old->Flink = entry;
    head->Blink = entry;
    GuestReleaseSpinLock(lock);
    return old == head ? nullptr : old;
}
extern "C" GuestListEntry* __stdcall K_ExfInterlockedRemoveHeadList(
    GuestListEntry* head, volatile LONG* lock) {
    if (!head || !lock) return nullptr;
    GuestAcquireSpinLock(lock);
    GuestListEntry* first = head->Flink;
    if (!first || first == head) { GuestReleaseSpinLock(lock); return nullptr; }
    GuestListEntry* next = first->Flink;
    if (!next) { GuestReleaseSpinLock(lock); return nullptr; }
    head->Flink = next;
    next->Blink = head;
    first->Flink = nullptr;
    first->Blink = nullptr;
    GuestReleaseSpinLock(lock);
    return first;
}

extern "C" PSLIST_ENTRY __fastcall K_InterlockedFlushSList(PSLIST_HEADER list) {
    return list ? InterlockedFlushSList(list) : nullptr;
}
extern "C" PSLIST_ENTRY __fastcall K_InterlockedPopEntrySList(PSLIST_HEADER list) {
    return list ? InterlockedPopEntrySList(list) : nullptr;
}
extern "C" PSLIST_ENTRY __fastcall K_InterlockedPushEntrySList(PSLIST_HEADER list, PSLIST_ENTRY entry) {
    return list && entry ? InterlockedPushEntrySList(list, entry) : nullptr;
}
extern "C" LONG __fastcall K_InterlockedCompareExchange(volatile LONG*p,LONG e,LONG c){return InterlockedCompareExchange(p,e,c);}
extern "C" LONG __fastcall K_InterlockedDecrement(volatile LONG*p){return InterlockedDecrement(p);}
extern "C" LONG __fastcall K_InterlockedIncrement(volatile LONG*p){return InterlockedIncrement(p);}
extern "C" LONG __fastcall K_InterlockedExchange(volatile LONG*p,LONG v){return InterlockedExchange(p,v);}
extern "C" LONG __fastcall K_InterlockedExchangeAdd(volatile LONG*p,LONG v){return InterlockedExchangeAdd(p,v);}
extern "C" DWORD __stdcall K_KeGetCurrentIrql(){return g_guestIrql;}
extern "C" HANDLE __stdcall K_KeGetCurrentThread(){return GetCurrentThread();}
extern "C" LONG __stdcall K_KeDelayExecutionThread(uint32_t, BOOL alertable, const LARGE_INTEGER* interval) {
    if (!interval) { SwitchToThread(); return 0; }
    const DWORD timeout = KernelTimeout(interval);
    const DWORD result = SleepEx(timeout, alertable ? TRUE : FALSE);
    // STATUS_USER_APC is returned when an alertable delay is interrupted.
    return result == WAIT_IO_COMPLETION ? LONG(0x000000C0u) : 0;
}
extern "C" uint64_t __stdcall K_KeQueryPerformanceCounter(uint64_t*frequency){
    LARGE_INTEGER c{},f{};QueryPerformanceCounter(&c);QueryPerformanceFrequency(&f);if(frequency)*frequency=f.QuadPart;return c.QuadPart;
}
extern "C" uint64_t __stdcall K_KeQueryPerformanceFrequency(){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}
extern "C" void __stdcall K_KeQuerySystemTime(LARGE_INTEGER*out){if(out){FILETIME ft{};GetSystemTimeAsFileTime(&ft);out->LowPart=ft.dwLowDateTime;out->HighPart=LONG(ft.dwHighDateTime);}}
extern "C" void* __stdcall K_MmAllocateContiguousMemory(uint32_t n){return VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
extern "C" void* __stdcall K_MmAllocateContiguousMemoryEx(uint32_t n,uint32_t,uint32_t,uint32_t,uint32_t){return K_MmAllocateContiguousMemory(n);}

extern "C" void* __stdcall K_MmAllocateSystemMemory(ULONG bytes, ULONG protect) {
    if (!bytes) return nullptr;
    DWORD page = protect ? protect : PAGE_READWRITE;
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, page);
}
extern "C" BOOL __stdcall K_MmFreeSystemMemory(void* base, ULONG) {
    return base ? VirtualFree(base, 0, MEM_RELEASE) : FALSE;
}
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
static bool DvdRelativePath(const GuestUnicodeString* name, std::string& relative) {
    if (!name || !name->Buffer || (name->Length & 1u)) return false;
    const std::wstring value(name->Buffer, name->Length / sizeof(WCHAR));
    constexpr wchar_t devicePrefix[] = L"\\Device\\Cdrom0";
    size_t start = std::wstring::npos;
    if (value.size() >= _countof(devicePrefix) - 1 &&
        _wcsnicmp(value.c_str(), devicePrefix, _countof(devicePrefix) - 1) == 0) {
        start = _countof(devicePrefix) - 1;
    } else if (value.size() >= 2 &&
        (value[0] == L'D' || value[0] == L'd') && value[1] == L':') {
        start = 2;
    } else {
        return false;
    }
    while (start < value.size() && (value[start] == L'\\' || value[start] == L'/')) ++start;
    const std::wstring tail = value.substr(start);
    if (tail.empty()) { relative.clear(); return true; }
    const int needed = WideCharToMultiByte(CP_ACP, 0, tail.data(),
        static_cast<int>(tail.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;
    relative.resize(static_cast<size_t>(needed));
    return WideCharToMultiByte(CP_ACP, 0, tail.data(),
        static_cast<int>(tail.size()), relative.data(), needed, nullptr, nullptr) == needed;
}

static bool FindDvdFile(HANDLE handle, GuestDvdFile& file) {
    std::lock_guard<std::mutex> lock(g_dvdFileMutex);
    const auto found = g_dvdFiles.find(handle);
    if (found == g_dvdFiles.end()) return false;
    file.path = found->second.path;
    file.size = found->second.size;
    file.startSector = found->second.startSector;
    file.attributes = found->second.attributes;
    file.position = found->second.position;
    file.directory = found->second.directory;
    return true;
}

static LONG OpenDvdFile(HANDLE* output, const std::string& path,
    uint32_t access, uint32_t disposition, GuestIoStatusBlock* io) {
    constexpr LONG statusDeviceNotReady = LONG(0xC00000A3u);
    constexpr LONG statusMediaWriteProtected = LONG(0xC00000A2u);
    constexpr LONG statusObjectNameNotFound = LONG(0xC0000034u);
    constexpr LONG statusInsufficientResources = LONG(0xC000009Au);
    if (!output) return LONG(0xC000000Du);
    if (!g_dvdReady) return statusDeviceNotReady;
    constexpr uint32_t writeMask = 0x40000000u | 0x10000000u | 0x00000002u |
        0x00000004u | 0x00000010u | 0x00000100u | 0x00010000u |
        0x00040000u | 0x00080000u;
    if ((access & writeMask) || disposition != 1u) return statusMediaWriteProtected;

    XDVDFS_FILE_INFO info{};
    bool isDirectory = path.empty();
    if (!path.empty()) {
        if (XDVDFS_GetFileInfo(path.c_str(), &info) != 0)
            return statusObjectNameNotFound;
        isDirectory = (info.attributes & XDVDFS_ATTR_DIRECTORY) != 0;
    }

    HANDLE handle = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    if (!handle) return statusInsufficientResources;
    GuestDvdFile file{};
    file.path = path;
    file.size = isDirectory ? 0 : info.size;
    file.startSector = info.startSector;
    file.attributes = isDirectory ? XDVDFS_ATTR_DIRECTORY : info.attributes;
    file.directory = isDirectory;
    if (isDirectory) {
        uint32_t required = 0;
        const int sizing = XDVDFS_ListDirectory(path.c_str(), nullptr, 0, &required);
        if (sizing != 0 && sizing != XDVDFS_ERROR_BUFFER_TOO_SMALL) {
            CloseHandle(handle);
            return statusObjectNameNotFound;
        }
        if (required > 100000u) { CloseHandle(handle); return statusInsufficientResources; }
        try { file.children.resize(required); }
        catch (...) { CloseHandle(handle); return statusInsufficientResources; }
        if (required && XDVDFS_ListDirectory(path.c_str(), file.children.data(),
            required, &required) != 0) {
            CloseHandle(handle);
            return statusObjectNameNotFound;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        g_dvdFiles.emplace(handle, std::move(file));
    }
    *output = handle;
    if (io) { io->Status = 0; io->Information = 1; }
    return 0;
}

static bool DvdWildcardMatch(const std::wstring& pattern, const std::wstring& value) {
    size_t p = 0, v = 0, star = std::wstring::npos, retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() &&
            (pattern[p] == L'?' || towupper(pattern[p]) == towupper(value[v]))) {
            ++p; ++v;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            star = p++;
            retry = v;
        } else if (star != std::wstring::npos) {
            p = star + 1;
            v = ++retry;
        } else return false;
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

DWORD FileAccess(uint32_t a){DWORD r=0;if(a&0x80000000u)r|=GENERIC_READ;if(a&0x40000000u)r|=GENERIC_WRITE;if(a&1)r|=GENERIC_READ;if(a&2)r|=GENERIC_WRITE;return r?r:GENERIC_READ;}
DWORD FileDisposition(uint32_t d){switch(d){case 0:return CREATE_ALWAYS;case 1:return OPEN_EXISTING;case 2:return CREATE_NEW;case 3:return OPEN_ALWAYS;case 4:return TRUNCATE_EXISTING;case 5:return CREATE_ALWAYS;default:return OPEN_EXISTING;}}
LONG NtCreateFileImpl(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,uint32_t attrs,uint32_t share,uint32_t disposition,uint32_t options){
    if(!out||!oa||!oa->ObjectName)return LONG(0xC000000Du);
    std::string dvdPath;
    if (DvdRelativePath(oa->ObjectName, dvdPath))
        return OpenDvdFile(out, dvdPath, access, disposition, io);
    std::wstring path=TranslateXboxPath(oa->ObjectName);if(path.empty())return LONG(0xC0000033u);
    DWORD flags=attrs?attrs:FILE_ATTRIBUTE_NORMAL;
    if(options&1)flags|=FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h=CreateFileW(path.c_str(),FileAccess(access),((share&1)?FILE_SHARE_READ:0)|((share&2)?FILE_SHARE_WRITE:0)|((share&4)?FILE_SHARE_DELETE:0),nullptr,FileDisposition(disposition),flags,nullptr);
    if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();if(io){io->Status=LONG(0xC0000001u);io->Information=0;}return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?LONG(0xC0000034u):e==ERROR_ACCESS_DENIED?LONG(0xC0000022u):LONG(0xC0000001u);}
    *out=h;if(io){io->Status=0;io->Information=1;}return 0;
}
extern "C" LONG __stdcall K_NtCreateFile(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,const LARGE_INTEGER*,uint32_t attrs,uint32_t share,uint32_t disposition,uint32_t options){return NtCreateFileImpl(out,access,oa,io,attrs,share,disposition,options);}
extern "C" LONG __stdcall K_NtOpenFile(HANDLE*out,uint32_t access,GuestObjectAttributes*oa,GuestIoStatusBlock*io,uint32_t share,uint32_t options){return NtCreateFileImpl(out,access,oa,io,FILE_ATTRIBUTE_NORMAL,share,1,options);}
struct GuestFileNetworkOpenInformation {
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER AllocationSize, EndOfFile;
    ULONG FileAttributes;
};
static LARGE_INTEGER FileTimeToLarge(const FILETIME& value) {
    ULARGE_INTEGER v{};
    v.LowPart = value.dwLowDateTime;
    v.HighPart = value.dwHighDateTime;
    LARGE_INTEGER result{};
    result.QuadPart = static_cast<LONGLONG>(v.QuadPart);
    return result;
}
extern "C" LONG __stdcall K_NtQueryFullAttributesFile(
    GuestObjectAttributes* oa, GuestFileNetworkOpenInformation* out) {
    if (!oa || !oa->ObjectName || !out || oa->Length < sizeof(GuestObjectAttributes))
        return LONG(0xC000000Du);
    if (oa->RootDirectory) return LONG(0xC00000BBu);
    std::string dvdPath;
    if (DvdRelativePath(oa->ObjectName, dvdPath)) {
        if (!g_dvdReady) return LONG(0xC00000A3u);
        GuestFileNetworkOpenInformation value{};
        if (dvdPath.empty()) {
            value.FileAttributes = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY;
        } else {
            XDVDFS_FILE_INFO entry{};
            if (XDVDFS_GetFileInfo(dvdPath.c_str(), &entry) != 0)
                return LONG(0xC0000034u);
            value.EndOfFile.QuadPart = entry.size;
            value.AllocationSize.QuadPart =
                (static_cast<LONGLONG>(entry.size) + 2047) & ~LONGLONG(2047);
            value.FileAttributes = FILE_ATTRIBUTE_READONLY;
            if (entry.attributes & XDVDFS_ATTR_DIRECTORY) value.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            else value.FileAttributes |= FILE_ATTRIBUTE_ARCHIVE;
            if (entry.attributes & XDVDFS_ATTR_HIDDEN) value.FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            if (entry.attributes & XDVDFS_ATTR_SYSTEM) value.FileAttributes |= FILE_ATTRIBUTE_SYSTEM;
        }
        *out = value;
        return 0;
    }
    const std::wstring path = TranslateXboxPath(oa->ObjectName);
    if (path.empty()) return LONG(0xC0000033u);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return LONG(0xC0000034u);
        if (error == ERROR_ACCESS_DENIED) return LONG(0xC0000022u);
        return LONG(0xC0000001u);
    }
    GuestFileNetworkOpenInformation value{};
    value.CreationTime = FileTimeToLarge(data.ftCreationTime);
    value.LastAccessTime = FileTimeToLarge(data.ftLastAccessTime);
    value.LastWriteTime = FileTimeToLarge(data.ftLastWriteTime);
    value.ChangeTime = value.LastWriteTime;
    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    value.EndOfFile.QuadPart = static_cast<LONGLONG>(size.QuadPart);
    value.AllocationSize = value.EndOfFile;
    value.FileAttributes = data.dwFileAttributes;
    *out = value;
    return 0;
}

extern "C" LONG __stdcall K_IoInvalidDeviceRequest(void*, void*) {
    return LONG(0xC0000010u); // STATUS_INVALID_DEVICE_REQUEST
}

extern "C" void __stdcall K_RtlCaptureContext(CONTEXT* context) {
    if (context) RtlCaptureContext(context);
}

extern "C" LONG __stdcall K_NtDeleteFile(GuestObjectAttributes* oa) {
    if (!oa || !oa->ObjectName) return LONG(0xC000000Du);
    std::string dvdPath;
    if (DvdRelativePath(oa->ObjectName, dvdPath))
        return g_dvdReady ? LONG(0xC00000A2u) : LONG(0xC00000A3u);
    const std::wstring path = TranslateXboxPath(oa->ObjectName);
    if (path.empty()) return LONG(0xC0000033u);
    if (DeleteFileW(path.c_str())) return 0;
    const DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) return LONG(0xC0000022u);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return LONG(0xC0000034u);
    if (error == ERROR_DIR_NOT_EMPTY) return LONG(0xC0000101u);
    if (error == ERROR_DIRECTORY) return LONG(0xC0000103u);
    return LONG(0xC0000001u);
}
extern "C" LONG __stdcall K_NtReadFile(HANDLE h,HANDLE event,void*,void*,GuestIoStatusBlock*io,void*buffer,uint32_t length,const LARGE_INTEGER*offset,uint32_t*){
    {
        std::unique_lock<std::mutex> lock(g_dvdFileMutex);
        auto dvd = g_dvdFiles.find(h);
        if (dvd != g_dvdFiles.end()) {
            if (dvd->second.directory) return LONG(0xC00000BAu);
            if (offset && offset->QuadPart < 0) return LONG(0xC000000Du);
            const uint64_t start = offset ? static_cast<uint64_t>(offset->QuadPart) : dvd->second.position;
            if (start > dvd->second.size) {
                if (io) { io->Status = LONG(0xC0000011u); io->Information = 0; }
                if (event) SetEvent(event);
                return LONG(0xC0000011u);
            }
            DWORD received = 0;
            const int result = XDVDFS_ReadFile(dvd->second.path.c_str(), start, buffer, length, &received);
            if (result != 0) {
                if (io) { io->Status = LONG(0xC0000001u); io->Information = 0; }
                return LONG(0xC0000001u);
            }
            dvd->second.position = start + received;
            if (io) { io->Status = (received || !length) ? 0 : LONG(0xC0000011u); io->Information = received; }
            if (event) SetEvent(event);
            return io ? io->Status : (received || !length ? 0 : LONG(0xC0000011u));
        }
    }
    std::lock_guard<std::mutex> fileLock(g_fileIoMutex);
    if(offset){if(offset->QuadPart<0)return LONG(0xC000000Du);LARGE_INTEGER p=*offset;if(!SetFilePointerEx(h,p,nullptr,FILE_BEGIN))return FileStatusFromWin32(GetLastError());}
    DWORD read=0;if(!ReadFile(h,buffer,length,&read,nullptr)){LONG status=FileStatusFromWin32(GetLastError());if(io){io->Status=status;io->Information=0;}return status;}
    if(io){io->Status=read?0:LONG(0xC0000011u);io->Information=read;}if(event)SetEvent(event);return read?0:LONG(0xC0000011u);
}
extern "C" LONG __stdcall K_NtWriteFile(HANDLE h,HANDLE event,void*,void*,GuestIoStatusBlock*io,const void*buffer,uint32_t length,const LARGE_INTEGER*offset,uint32_t*){
    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        if (g_dvdFiles.find(h) != g_dvdFiles.end()) {
            constexpr LONG status = LONG(0xC00000A2u);
            if (io) { io->Status = status; io->Information = 0; }
            if (event) SetEvent(event);
            return status;
        }
    }
    std::lock_guard<std::mutex> fileLock(g_fileIoMutex);
    if(offset){if(offset->QuadPart<0)return LONG(0xC000000Du);LARGE_INTEGER p=*offset;if(!SetFilePointerEx(h,p,nullptr,FILE_BEGIN))return FileStatusFromWin32(GetLastError());}
    DWORD written=0;if(!WriteFile(h,buffer,length,&written,nullptr)){LONG status=FileStatusFromWin32(GetLastError());if(io){io->Status=status;io->Information=0;}return status;}
    if(io){io->Status=0;io->Information=written;}if(event)SetEvent(event);return 0;
}
extern "C" LONG __stdcall K_NtFlushBuffersFile(HANDLE h,GuestIoStatusBlock*io){
    GuestDvdFile dvd{};
    if (FindDvdFile(h, dvd)) { if(io){io->Status=0;io->Information=0;} return 0; }
    std::lock_guard<std::mutex> fileLock(g_fileIoMutex);
    BOOL ok=FlushFileBuffers(h);if(io){io->Status=ok?0:LONG(0xC0000001u);io->Information=0;}return ok?0:LONG(0xC0000001u);
}

// NT FILE_INFORMATION_CLASS values used by retail Xbox titles.
struct GuestFileBasicInformation {
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    ULONG FileAttributes;
};
struct GuestFileStandardInformation {
    LARGE_INTEGER AllocationSize, EndOfFile;
    ULONG NumberOfLinks;
    BOOLEAN DeletePending, Directory;
};
struct GuestFilePositionInformation { LARGE_INTEGER CurrentByteOffset; };
struct GuestFileInternalInformation { LARGE_INTEGER IndexNumber; };
struct GuestFileDispositionInformation { BOOLEAN DeleteFile; };
struct GuestFileEndOfFileInformation { LARGE_INTEGER EndOfFile; };
struct GuestFileNameInformation { ULONG FileNameLength; WCHAR FileName[1]; };

static LONG FileStatusFromWin32(DWORD e) {
    switch (e) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return LONG(0xC0000034u);
    case ERROR_ACCESS_DENIED: return LONG(0xC0000022u);
    case ERROR_INVALID_HANDLE: return LONG(0xC0000008u);
    case ERROR_INVALID_PARAMETER: return LONG(0xC000000Du);
    case ERROR_DISK_FULL: return LONG(0xC000007Fu);
    default: return LONG(0xC0000001u);
    }
}
extern "C" LONG __stdcall K_NtQueryInformationFile(HANDLE h, GuestIoStatusBlock* io,
                                                       void* out, ULONG length, ULONG infoClass) {
    if (!h || h == INVALID_HANDLE_VALUE || !out) return LONG(0xC000000Du);
    GuestDvdFile dvdFile{};
    if (FindDvdFile(h, dvdFile)) {
        ULONG required = 0;
        switch (infoClass) {
        case 4: required = sizeof(GuestFileBasicInformation); break;
        case 5: required = sizeof(GuestFileStandardInformation); break;
        case 6: required = sizeof(GuestFileInternalInformation); break;
        case 9: required = sizeof(ULONG); break;
        case 14: required = sizeof(GuestFilePositionInformation); break;
        default: return LONG(0xC0000003u);
        }
        if (infoClass == 9) {
            const int chars = dvdFile.path.empty() ? 0 : MultiByteToWideChar(CP_ACP, 0,
                dvdFile.path.data(), static_cast<int>(dvdFile.path.size()), nullptr, 0);
            if (chars < 0) return LONG(0xC0000001u);
            const ULONG bytes = static_cast<ULONG>(chars * sizeof(WCHAR));
            required = sizeof(ULONG) + bytes;
            if (length < required) return LONG(0xC0000004u);
            std::memset(out, 0, required);
            auto* p = static_cast<GuestFileNameInformation*>(out);
            p->FileNameLength = bytes;
            if (bytes) MultiByteToWideChar(CP_ACP, 0, dvdFile.path.data(),
                static_cast<int>(dvdFile.path.size()), p->FileName, chars);
        } else {
            if (length < required) return LONG(0xC0000004u);
            std::memset(out, 0, required);
            if (infoClass == 4) {
                static_cast<GuestFileBasicInformation*>(out)->FileAttributes =
                    FILE_ATTRIBUTE_READONLY | (dvdFile.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE);
            } else if (infoClass == 5) {
                auto* p = static_cast<GuestFileStandardInformation*>(out);
                p->EndOfFile.QuadPart = dvdFile.size;
                p->AllocationSize.QuadPart = (static_cast<LONGLONG>(dvdFile.size) + 2047) & ~LONGLONG(2047);
                p->NumberOfLinks = 1;
                p->Directory = dvdFile.directory ? TRUE : FALSE;
            } else if (infoClass == 6) {
                static_cast<GuestFileInternalInformation*>(out)->IndexNumber.QuadPart =
                    (static_cast<ULONGLONG>(dvdFile.startSector) << 32) | dvdFile.size;
            } else {
                static_cast<GuestFilePositionInformation*>(out)->CurrentByteOffset.QuadPart =
                    static_cast<LONGLONG>(dvdFile.position);
            }
        }
        if (io) { io->Status = 0; io->Information = required; }
        return 0;
    }
    ULONG required = 0;
    switch (infoClass) {
    case 4: required = sizeof(GuestFileBasicInformation); break;
    case 5: required = sizeof(GuestFileStandardInformation); break;
    case 6: required = sizeof(GuestFileInternalInformation); break;
    case 9: required = sizeof(ULONG); break;
    case 14: required = sizeof(GuestFilePositionInformation); break;
    default: return LONG(0xC0000003u);
    }
    if (length < required) return LONG(0xC0000004u);
    std::memset(out, 0, required);
    FILETIME creation{}, access{}, write{};
    BY_HANDLE_FILE_INFORMATION native{};
    if (!GetFileInformationByHandle(h, &native)) return FileStatusFromWin32(GetLastError());
    auto toLarge = [](const FILETIME& ft) { ULARGE_INTEGER v{}; v.LowPart=ft.dwLowDateTime; v.HighPart=ft.dwHighDateTime; LARGE_INTEGER r{}; r.QuadPart=static_cast<LONGLONG>(v.QuadPart); return r; };
    if (infoClass == 4) {
        auto* p = static_cast<GuestFileBasicInformation*>(out);
        p->CreationTime=toLarge(native.ftCreationTime); p->LastAccessTime=toLarge(native.ftLastAccessTime);
        p->LastWriteTime=toLarge(native.ftLastWriteTime); p->ChangeTime=p->LastWriteTime;
        p->FileAttributes=native.dwFileAttributes;
    } else if (infoClass == 5) {
        auto* p = static_cast<GuestFileStandardInformation*>(out);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size)) return FileStatusFromWin32(GetLastError());
        p->EndOfFile=size; p->AllocationSize.QuadPart=(size.QuadPart+4095)&~LONGLONG(4095);
        p->NumberOfLinks=native.nNumberOfLinks; p->Directory=(native.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    } else if (infoClass == 6) {
        static_cast<GuestFileInternalInformation*>(out)->IndexNumber.QuadPart =
            (static_cast<ULONGLONG>(native.nFileIndexHigh)<<32)|native.nFileIndexLow;
    } else if (infoClass == 9) {
        auto* p=static_cast<GuestFileNameInformation*>(out);
        WCHAR path[MAX_PATH*4]{};
        DWORD n=GetFinalPathNameByHandleW(h,path,static_cast<DWORD>(sizeof(path)/sizeof(path[0])),FILE_NAME_NORMALIZED);
        if (!n || n>=sizeof(path)/sizeof(path[0])) return FileStatusFromWin32(GetLastError());
        std::wstring name(path,n); const size_t slash=name.find_last_of(L"\\/");
        if(slash!=std::wstring::npos) name.erase(0,slash+1);
        const ULONG bytes=static_cast<ULONG>(name.size()*sizeof(WCHAR));
        if(length<sizeof(ULONG)+bytes) return LONG(0xC0000004u);
        p->FileNameLength=bytes; std::memcpy(p->FileName,name.data(),bytes);
        required=sizeof(ULONG)+bytes;
    } else {
        LARGE_INTEGER zero{}, position{};
        if (!SetFilePointerEx(h, zero, &position, FILE_CURRENT)) return FileStatusFromWin32(GetLastError());
        static_cast<GuestFilePositionInformation*>(out)->CurrentByteOffset=position;
    }
    if(io){io->Status=0;io->Information=required;}
    return 0;
}
extern "C" LONG __stdcall K_NtSetInformationFile(HANDLE h, GuestIoStatusBlock* io,
                                                     void* in, ULONG length, ULONG infoClass) {
    if (!h || h == INVALID_HANDLE_VALUE || !in) return LONG(0xC000000Du);
    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        auto dvd = g_dvdFiles.find(h);
        if (dvd != g_dvdFiles.end()) {
            if (infoClass == 14) {
                if (length < sizeof(GuestFilePositionInformation)) return LONG(0xC0000004u);
                const LONGLONG position = static_cast<GuestFilePositionInformation*>(in)->CurrentByteOffset.QuadPart;
                if (position < 0) return LONG(0xC000000Du);
                dvd->second.position = static_cast<uint64_t>(position);
                if (io) { io->Status = 0; io->Information = 0; }
                return 0;
            }
            if (io) { io->Status = LONG(0xC00000A2u); io->Information = 0; }
            return LONG(0xC00000A2u);
        }
    }
    BOOL ok=FALSE;
    if(infoClass==4) {
        if(length<sizeof(GuestFileBasicInformation)) return LONG(0xC0000004u);
        auto* p=static_cast<GuestFileBasicInformation*>(in);
        FILETIME c{DWORD(p->CreationTime.QuadPart),DWORD(ULONGLONG(p->CreationTime.QuadPart)>>32)};
        FILETIME a{DWORD(p->LastAccessTime.QuadPart),DWORD(ULONGLONG(p->LastAccessTime.QuadPart)>>32)};
        FILETIME w{DWORD(p->LastWriteTime.QuadPart),DWORD(ULONGLONG(p->LastWriteTime.QuadPart)>>32)};
        ok=SetFileTime(h,p->CreationTime.QuadPart?&c:nullptr,p->LastAccessTime.QuadPart?&a:nullptr,p->LastWriteTime.QuadPart?&w:nullptr);
        if(ok && p->FileAttributes) {
            WCHAR path[MAX_PATH*4]{};
            DWORD n=GetFinalPathNameByHandleW(h,path,static_cast<DWORD>(sizeof(path)/sizeof(path[0])),FILE_NAME_NORMALIZED);
            if(!n || n>=sizeof(path)/sizeof(path[0])) ok=FALSE;
            else ok=SetFileAttributesW(path,p->FileAttributes);
        }
    } else if(infoClass==13) {
        if(length<sizeof(GuestFileDispositionInformation)) return LONG(0xC0000004u);
        FILE_DISPOSITION_INFO native{}; native.DeleteFile=static_cast<GuestFileDispositionInformation*>(in)->DeleteFile;
        ok=SetFileInformationByHandle(h,FileDispositionInfo,&native,sizeof(native));
    } else if(infoClass==14) {
        if(length<sizeof(GuestFilePositionInformation)) return LONG(0xC0000004u);
        LARGE_INTEGER position=static_cast<GuestFilePositionInformation*>(in)->CurrentByteOffset;
        ok=SetFilePointerEx(h,position,nullptr,FILE_BEGIN);
    } else if(infoClass==20) {
        if(length<sizeof(GuestFileEndOfFileInformation)) return LONG(0xC0000004u);
        LARGE_INTEGER size=static_cast<GuestFileEndOfFileInformation*>(in)->EndOfFile;
        ok=SetFilePointerEx(h,size,nullptr,FILE_BEGIN) && SetEndOfFile(h);
    } else return LONG(0xC0000003u);
    if(!ok){LONG status=FileStatusFromWin32(GetLastError());if(io){io->Status=status;io->Information=0;}return status;}
    if(io){io->Status=0;io->Information=0;}
    return 0;
}
extern "C" LONG __stdcall K_NtQueryVolumeInformationFile(
    HANDLE h, GuestIoStatusBlock* io, void* out, ULONG length, ULONG infoClass) {
    constexpr LONG statusInvalidParameter = LONG(0xC000000Du);
    constexpr LONG statusInfoLengthMismatch = LONG(0xC0000004u);
    constexpr LONG statusBufferOverflow = LONG(0x80000005u);
    if (!h || h == INVALID_HANDLE_VALUE || !out) return statusInvalidParameter;

    GuestDvdFile dvdFile{};
    if (FindDvdFile(h, dvdFile)) {
        auto writeField = [out](ULONG offset, const void* value, ULONG bytes) {
            std::memcpy(static_cast<uint8_t*>(out) + offset, value, bytes);
        };
        ULONG information = 0;
        switch (infoClass) {
        case 1: {
            constexpr ULONG labelOffset = 18;
            if (length < labelOffset) return statusInfoLengthMismatch;
            const LARGE_INTEGER creationTime{};
            const ULONG serial = 0, labelLength = 0;
            const BOOLEAN supportsObjects = FALSE;
            std::memset(out, 0, labelOffset);
            writeField(0, &creationTime, sizeof(creationTime));
            writeField(8, &serial, sizeof(serial));
            writeField(12, &labelLength, sizeof(labelLength));
            writeField(16, &supportsObjects, sizeof(supportsObjects));
            information = labelOffset;
            break;
        }
        case 3: {
            constexpr ULONG required = 24;
            if (length < required) return statusInfoLengthMismatch;
            LARGE_INTEGER total{}; total.QuadPart = static_cast<LONGLONG>(g_dvdInfo.imageSize / 2048u);
            LARGE_INTEGER available{};
            const ULONG sectorsPerAllocationUnit = 1, bytesPerSector = 2048;
            std::memset(out, 0, required);
            writeField(0, &total, sizeof(total)); writeField(8, &available, sizeof(available));
            writeField(16, &sectorsPerAllocationUnit, sizeof(sectorsPerAllocationUnit));
            writeField(20, &bytesPerSector, sizeof(bytesPerSector));
            information = required;
            break;
        }
        case 4: {
            constexpr ULONG required = 8;
            if (length < required) return statusInfoLengthMismatch;
            const ULONG deviceType = 2, characteristics = 3;
            writeField(0, &deviceType, sizeof(deviceType)); writeField(4, &characteristics, sizeof(characteristics));
            information = required;
            break;
        }
        case 5: {
            constexpr ULONG nameOffset = 12;
            constexpr wchar_t fsName[] = L"XDVDFS";
            constexpr ULONG nameBytes = (static_cast<ULONG>(_countof(fsName)) - 1) * sizeof(WCHAR);
            if (length < nameOffset) return statusInfoLengthMismatch;
            const ULONG attributes = 0, maximumComponent = 255;
            const ULONG copiedBytes = (std::min)(nameBytes, length - nameOffset) & ~ULONG(1);
            std::memset(out, 0, nameOffset + copiedBytes);
            writeField(0, &attributes, sizeof(attributes)); writeField(4, &maximumComponent, sizeof(maximumComponent));
            writeField(8, &nameBytes, sizeof(nameBytes));
            if (copiedBytes) writeField(nameOffset, fsName, copiedBytes);
            information = nameOffset + copiedBytes;
            if (copiedBytes != nameBytes) {
                if (io) { io->Status = statusBufferOverflow; io->Information = information; }
                return statusBufferOverflow;
            }
            break;
        }
        default: return LONG(0xC0000003u);
        }
        if (io) { io->Status = 0; io->Information = information; }
        return 0;
    }

    WCHAR finalPath[MAX_PATH * 4]{};
    const DWORD pathLength = GetFinalPathNameByHandleW(
        h, finalPath, static_cast<DWORD>(_countof(finalPath)), FILE_NAME_NORMALIZED);
    if (!pathLength || pathLength >= std::size(finalPath))
        return FileStatusFromWin32(GetLastError());

    WCHAR volumeRoot[MAX_PATH * 4]{};
    if (!GetVolumePathNameW(finalPath, volumeRoot, static_cast<DWORD>(_countof(volumeRoot))))
        return FileStatusFromWin32(GetLastError());

    WCHAR volumeLabel[MAX_PATH]{};
    WCHAR fileSystemName[MAX_PATH]{};
    DWORD serial = 0, maximumComponent = 0, fileSystemFlags = 0;
    if (!GetVolumeInformationW(volumeRoot, volumeLabel, static_cast<DWORD>(_countof(volumeLabel)),
                               &serial, &maximumComponent, &fileSystemFlags,
                               fileSystemName, static_cast<DWORD>(_countof(fileSystemName))))
        return FileStatusFromWin32(GetLastError());

    auto writeField = [out](ULONG offset, const void* value, ULONG bytes) {
        std::memcpy(static_cast<uint8_t*>(out) + offset, value, bytes);
    };
    ULONG information = 0;
    switch (infoClass) {
    case 1: { // FileFsVolumeInformation
        constexpr ULONG labelOffset = 18;
        if (length < labelOffset) return statusInfoLengthMismatch;
        const ULONG labelBytes = static_cast<ULONG>(wcslen(volumeLabel) * sizeof(WCHAR));
        const ULONG copiedBytes = std::min(labelBytes, length - labelOffset) & ~ULONG(1);
        LARGE_INTEGER creationTime{};
        const ULONG labelLength = labelBytes;
        const BOOLEAN supportsObjects = FALSE;
        std::memset(out, 0, labelOffset + copiedBytes);
        writeField(0, &creationTime, sizeof(creationTime));
        writeField(8, &serial, sizeof(serial));
        writeField(12, &labelLength, sizeof(labelLength));
        writeField(16, &supportsObjects, sizeof(supportsObjects));
        if (copiedBytes) writeField(labelOffset, volumeLabel, copiedBytes);
        information = labelOffset + copiedBytes;
        if (copiedBytes != labelBytes) {
            if (io) { io->Status = statusBufferOverflow; io->Information = information; }
            return statusBufferOverflow;
        }
        break;
    }
    case 3: { // FileFsSizeInformation
        constexpr ULONG required = 24;
        if (length < required) return statusInfoLengthMismatch;
        DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
        if (!GetDiskFreeSpaceW(volumeRoot, &sectorsPerCluster, &bytesPerSector,
                               &freeClusters, &totalClusters))
            return FileStatusFromWin32(GetLastError());
        LARGE_INTEGER total{}; total.QuadPart = static_cast<LONGLONG>(totalClusters);
        LARGE_INTEGER available{}; available.QuadPart = static_cast<LONGLONG>(freeClusters);
        std::memset(out, 0, required);
        writeField(0, &total, sizeof(total));
        writeField(8, &available, sizeof(available));
        writeField(16, &sectorsPerCluster, sizeof(sectorsPerCluster));
        writeField(20, &bytesPerSector, sizeof(bytesPerSector));
        information = required;
        break;
    }
    case 4: { // FileFsDeviceInformation
        constexpr ULONG required = 8;
        if (length < required) return statusInfoLengthMismatch;
        const ULONG deviceType = 7; // FILE_DEVICE_DISK
        const ULONG characteristics = 0;
        writeField(0, &deviceType, sizeof(deviceType));
        writeField(4, &characteristics, sizeof(characteristics));
        information = required;
        break;
    }
    case 5: { // FileFsAttributeInformation
        constexpr ULONG nameOffset = 12;
        if (length < nameOffset) return statusInfoLengthMismatch;
        const ULONG nameBytes = static_cast<ULONG>(wcslen(fileSystemName) * sizeof(WCHAR));
        const ULONG copiedBytes = std::min(nameBytes, length - nameOffset) & ~ULONG(1);
        std::memset(out, 0, nameOffset + copiedBytes);
        writeField(0, &fileSystemFlags, sizeof(fileSystemFlags));
        writeField(4, &maximumComponent, sizeof(maximumComponent));
        writeField(8, &nameBytes, sizeof(nameBytes));
        if (copiedBytes) writeField(nameOffset, fileSystemName, copiedBytes);
        information = nameOffset + copiedBytes;
        if (copiedBytes != nameBytes) {
            if (io) { io->Status = statusBufferOverflow; io->Information = information; }
            return statusBufferOverflow;
        }
        break;
    }
    default:
        return LONG(0xC0000003u); // STATUS_INVALID_INFO_CLASS
    }
    if (io) { io->Status = 0; io->Information = information; }
    return 0;
}

extern "C" LONG __stdcall K_NtClose(HANDLE h){
    bool dvdHandle = false;
    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        auto found = g_dvdFiles.find(h);
        if (found != g_dvdFiles.end()) { g_dvdFiles.erase(found); dvdHandle = true; }
    }
    CloseGuestDirectorySearch(h);
    return CloseHandle(h) ? 0 : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtCreateEvent(HANDLE*out,ACCESS_MASK,void*,uint32_t type,BOOL initial){
    if(!out)return LONG(0xC000000Du);HANDLE h=CreateEventW(nullptr,type==0,initial,nullptr);if(!h)return LONG(0xC0000001u);*out=h;return 0;
}
extern "C" LONG __stdcall K_NtSetEvent(HANDLE h,LONG*previous){if(previous)*previous=0;return SetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtClearEvent(HANDLE h){return ResetEvent(h)?0:LONG(0xC0000008u);}
extern "C" LONG __stdcall K_NtWaitForSingleObject(HANDLE h, BOOL alertable, const LARGE_INTEGER* timeout) {
    if (!h || h == INVALID_HANDLE_VALUE) return LONG(0xC0000008u);
    const DWORD result = WaitForSingleObjectEx(h, KernelTimeout(timeout), alertable ? TRUE : FALSE);
    if (result == WAIT_OBJECT_0) return 0;
    if (result == WAIT_TIMEOUT) return LONG(0x00000102u);
    if (result == WAIT_IO_COMPLETION) return LONG(0x000000C0u); // STATUS_USER_APC
    return LONG(0xC0000008u);
}
struct GuestAnsiString { USHORT Length,MaximumLength; char* Buffer; };

struct GuestFileDirectoryInformation {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER EndOfFile, AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    CHAR FileName[1];
};
static_assert(offsetof(GuestFileDirectoryInformation, FileName) == 64,
              "Xbox FILE_DIRECTORY_INFORMATION filename offset must be 0x40");
static bool SkipDotDirectoryEntries(HANDLE search, WIN32_FIND_DATAW& data) {
    while (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
        if (!FindNextFileW(search, &data)) return false;
    }
    return true;
}
static LONG SetDirectoryIoStatus(GuestIoStatusBlock* io, LONG status, ULONG information = 0) {
    if (io) { io->Status = status; io->Information = information; }
    return status;
}
extern "C" LONG __stdcall K_NtQueryDirectoryFile(
    HANDLE file, HANDLE event, void* apcRoutine, void*, GuestIoStatusBlock* io,
    void* information, ULONG length, ULONG informationClass,
    GuestAnsiString* fileMask, BOOLEAN restartScan) {
    constexpr LONG statusInvalidParameter = LONG(0xC000000Du);
    constexpr LONG statusInvalidHandle = LONG(0xC0000008u);
    constexpr LONG statusNotSupported = LONG(0xC00000BBu);
    constexpr LONG statusNoMoreFiles = LONG(0x80000006u);
    constexpr LONG statusBufferOverflow = LONG(0x80000005u);
    if (!file || file == INVALID_HANDLE_VALUE) return SetDirectoryIoStatus(io, statusInvalidHandle);
    if (!information || informationClass != 1 || length < 64)
        return SetDirectoryIoStatus(io, informationClass != 1 ? LONG(0xC0000003u) : statusInvalidParameter);
    if (apcRoutine) return SetDirectoryIoStatus(io, statusNotSupported);
    if (fileMask && ((fileMask->Length && !fileMask->Buffer) ||
                     fileMask->Length > fileMask->MaximumLength))
        return SetDirectoryIoStatus(io, statusInvalidParameter);

    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        auto dvd = g_dvdFiles.find(file);
        if (dvd != g_dvdFiles.end()) {
            if (!dvd->second.directory) return SetDirectoryIoStatus(io, LONG(0xC0000103u));
            if (restartScan) dvd->second.directoryIndex = 0;
            std::wstring wanted;
            if (fileMask && fileMask->Length) {
                const int chars = MultiByteToWideChar(CP_ACP, 0, fileMask->Buffer,
                    fileMask->Length, nullptr, 0);
                if (chars <= 0) return SetDirectoryIoStatus(io, statusInvalidParameter);
                wanted.resize(static_cast<size_t>(chars));
                if (!MultiByteToWideChar(CP_ACP, 0, fileMask->Buffer,
                    fileMask->Length, wanted.data(), chars))
                    return SetDirectoryIoStatus(io, statusInvalidParameter);
                if (wanted == L"*.*") wanted = L"*";
            }
            while (dvd->second.directoryIndex < dvd->second.children.size()) {
                const uint32_t index = dvd->second.directoryIndex;
                const XDVDFS_FILE_INFO& child = dvd->second.children[index];
                const int chars = MultiByteToWideChar(CP_ACP, 0, child.name,
                    child.nameLength, nullptr, 0);
                if (chars <= 0) { ++dvd->second.directoryIndex; continue; }
                std::wstring childName(static_cast<size_t>(chars), L'\\0');
                MultiByteToWideChar(CP_ACP, 0, child.name, child.nameLength,
                    childName.data(), chars);
                if (!wanted.empty() && !DvdWildcardMatch(wanted, childName)) {
                    ++dvd->second.directoryIndex;
                    continue;
                }
                const size_t required = offsetof(GuestFileDirectoryInformation, FileName) + child.nameLength;
                if (required > length)
                    return SetDirectoryIoStatus(io, statusBufferOverflow, static_cast<ULONG>(required));
                auto* out = static_cast<GuestFileDirectoryInformation*>(information);
                std::memset(out, 0, required);
                out->FileIndex = index + 1;
                out->EndOfFile.QuadPart = child.size;
                out->AllocationSize.QuadPart =
                    (static_cast<LONGLONG>(child.size) + 2047) & ~LONGLONG(2047);
                out->FileAttributes = 0;
                if (child.attributes & XDVDFS_ATTR_DIRECTORY) out->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
                if (child.attributes & XDVDFS_ATTR_READONLY) out->FileAttributes |= FILE_ATTRIBUTE_READONLY;
                if (child.attributes & XDVDFS_ATTR_HIDDEN) out->FileAttributes |= FILE_ATTRIBUTE_HIDDEN;
                if (child.attributes & XDVDFS_ATTR_SYSTEM) out->FileAttributes |= FILE_ATTRIBUTE_SYSTEM;
                if (child.attributes & XDVDFS_ATTR_ARCHIVE) out->FileAttributes |= FILE_ATTRIBUTE_ARCHIVE;
                if (!out->FileAttributes) out->FileAttributes = FILE_ATTRIBUTE_NORMAL;
                out->FileNameLength = child.nameLength;
                if (child.nameLength) std::memcpy(out->FileName, child.name, child.nameLength);
                ++dvd->second.directoryIndex;
                if (event) SetEvent(event);
                return SetDirectoryIoStatus(io, 0, static_cast<ULONG>(required));
            }
            return SetDirectoryIoStatus(io, statusNoMoreFiles);
        }
    }

    std::wstring directory;
    WCHAR pathBuffer[32768]{};
    const DWORD pathLength = GetFinalPathNameByHandleW(file, pathBuffer, _countof(pathBuffer), FILE_NAME_NORMALIZED);
    if (!pathLength || pathLength >= _countof(pathBuffer))
        return SetDirectoryIoStatus(io, statusInvalidHandle);
    directory.assign(pathBuffer, pathLength);
    if (directory.rfind(L"\\\\?\\UNC\\", 0) == 0)
        directory = L"\\\\" + directory.substr(8);
    else if (directory.rfind(L"\\\\?\\", 0) == 0)
        directory.erase(0, 4);
    if (directory.empty()) return SetDirectoryIoStatus(io, LONG(0xC000003Au));

    std::wstring mask = L"*";
    if (fileMask && fileMask->Length) {
        const int chars = MultiByteToWideChar(CP_ACP, 0, fileMask->Buffer, fileMask->Length, nullptr, 0);
        if (chars <= 0) return SetDirectoryIoStatus(io, statusInvalidParameter);
        mask.resize(chars);
        if (!MultiByteToWideChar(CP_ACP, 0, fileMask->Buffer, fileMask->Length, mask.data(), chars))
            return SetDirectoryIoStatus(io, statusInvalidParameter);
    }
    if (directory.back() != L'\\' && directory.back() != L'/') directory.push_back(L'\\');
    const std::wstring pattern = directory + mask;

    std::lock_guard<std::mutex> lock(g_directorySearchMutex);
    auto it = g_directorySearches.find(file);
    if (restartScan && it != g_directorySearches.end()) {
        if (it->second.search != INVALID_HANDLE_VALUE) FindClose(it->second.search);
        g_directorySearches.erase(it);
        it = g_directorySearches.end();
    }
    if (it == g_directorySearches.end()) {
        GuestDirectorySearch state{};
        state.search = FindFirstFileW(pattern.c_str(), &state.current);
        if (state.search == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES)
                return SetDirectoryIoStatus(io, statusNoMoreFiles);
            if (error == ERROR_PATH_NOT_FOUND)
                return SetDirectoryIoStatus(io, LONG(0xC000003Au));
            return SetDirectoryIoStatus(io, LONG(0xC0000001u));
        }
        state.currentValid = SkipDotDirectoryEntries(state.search, state.current);
        if (!state.currentValid) {
            FindClose(state.search);
            return SetDirectoryIoStatus(io, statusNoMoreFiles);
        }
        it = g_directorySearches.emplace(file, std::move(state)).first;
    } else if (!it->second.currentValid) {
        if (!FindNextFileW(it->second.search, &it->second.current) ||
            !SkipDotDirectoryEntries(it->second.search, it->second.current)) {
            FindClose(it->second.search);
            g_directorySearches.erase(it);
            return SetDirectoryIoStatus(io, statusNoMoreFiles);
        }
        it->second.currentValid = true;
    }

    GuestDirectorySearch& state = it->second;
    const size_t nameChars = wcslen(state.current.cFileName);
    const int nameBytes = nameChars ? WideCharToMultiByte(CP_ACP, 0, state.current.cFileName,
        static_cast<int>(nameChars), nullptr, 0, nullptr, nullptr) : 0;
    if (nameBytes < 0) return SetDirectoryIoStatus(io, LONG(0xC0000001u));
    const size_t required = offsetof(GuestFileDirectoryInformation, FileName) + static_cast<size_t>(nameBytes);
    if (required > length) return SetDirectoryIoStatus(io, statusBufferOverflow, static_cast<ULONG>(required));

    auto* out = static_cast<GuestFileDirectoryInformation*>(information);
    std::memset(out, 0, required);
    out->CreationTime = FileTimeToLarge(state.current.ftCreationTime);
    out->LastAccessTime = FileTimeToLarge(state.current.ftLastAccessTime);
    out->LastWriteTime = FileTimeToLarge(state.current.ftLastWriteTime);
    out->ChangeTime = out->LastWriteTime;
    ULARGE_INTEGER size{};
    size.HighPart = state.current.nFileSizeHigh;
    size.LowPart = state.current.nFileSizeLow;
    out->EndOfFile.QuadPart = static_cast<LONGLONG>(size.QuadPart);
    out->AllocationSize = out->EndOfFile;
    out->FileAttributes = state.current.dwFileAttributes;
    out->FileNameLength = static_cast<ULONG>(nameBytes);
    if (nameBytes && !WideCharToMultiByte(CP_ACP, 0, state.current.cFileName,
        static_cast<int>(nameChars), out->FileName, nameBytes, nullptr, nullptr))
        return SetDirectoryIoStatus(io, LONG(0xC0000001u));
    state.currentValid = false;
    if (event) SetEvent(event);
    return SetDirectoryIoStatus(io, 0, static_cast<ULONG>(required));
}


static bool ParseUnsigned(const char* text, ULONG base, ULONG* value) {
    if (!text || !value || (base != 0 && (base < 2 || base > 36))) return false;
    while (*text && std::isspace(static_cast<unsigned char>(*text))) ++text;
    if (!*text || *text == '-') return false;
    errno = 0;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(text, &end, static_cast<int>(base));
    if (errno == ERANGE || end == text || parsed > 0xFFFFFFFFul) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return false;
    *value = static_cast<ULONG>(parsed);
    return true;
}
static bool ParseUnsigned(const wchar_t* text, ULONG chars, ULONG base, ULONG* value) {
    if (!text || !value || (base != 0 && (base < 2 || base > 36))) return false;
    std::wstring input(text, chars);
    size_t first = input.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos || input[first] == L'-') return false;
    errno = 0;
    wchar_t* end = nullptr;
    unsigned long parsed = std::wcstoul(input.c_str(), &end, static_cast<int>(base));
    if (errno == ERANGE || end == input.c_str() || parsed > 0xFFFFFFFFul) return false;
    while (*end && iswspace(*end)) ++end;
    if (*end) return false;
    *value = static_cast<ULONG>(parsed);
    return true;
}
static std::string FormatUnsigned(ULONG value, ULONG base) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string out;
    do { out.push_back(digits[value % base]); value /= base; } while (value);
    std::reverse(out.begin(), out.end());
    return out;
}
extern "C" void __stdcall K_RtlRaiseStatus(LONG status);

// Original Xbox RTL 64-bit arithmetic exports (ordinals 281-283).
extern "C" LARGE_INTEGER __stdcall K_RtlExtendedIntegerMultiply(LARGE_INTEGER multiplicand, LONG multiplier) {
    LARGE_INTEGER result{};
    const uint64_t product = static_cast<uint64_t>(multiplicand.QuadPart) *
                             static_cast<uint64_t>(static_cast<int64_t>(multiplier));
    result.QuadPart = static_cast<LONGLONG>(product);
    return result;
}
extern "C" LARGE_INTEGER __stdcall K_RtlExtendedLargeIntegerDivide(
    LARGE_INTEGER dividend, ULONG divisor, ULONG* remainder) {
    LARGE_INTEGER result = dividend;
    if (!divisor) {
        K_RtlRaiseStatus(LONG(0xC0000094u));
        return result;
    }
    const uint64_t bits = static_cast<uint64_t>(dividend.QuadPart);
    result.QuadPart = static_cast<LONGLONG>(bits / divisor);
    if (remainder) *remainder = static_cast<ULONG>(bits % divisor);
    return result;
}
extern "C" LARGE_INTEGER __stdcall K_RtlExtendedMagicDivide(
    LARGE_INTEGER dividend, LARGE_INTEGER magicDivisor, CHAR shiftCount) {
    LARGE_INTEGER result{};
    const uint8_t shift = static_cast<uint8_t>(shiftCount);
    if (shift >= 64) {
        K_RtlRaiseStatus(LONG(0xC000000Du));
        return result;
    }
    const int64_t signedDividend = dividend.QuadPart;
    const bool positive = signedDividend >= 0;
    const uint64_t magnitude = positive
        ? static_cast<uint64_t>(signedDividend)
        : static_cast<uint64_t>(-(signedDividend + 1)) + 1u;
    const uint64_t inverse = static_cast<uint64_t>(magicDivisor.QuadPart);
    const uint64_t dh = magnitude >> 32, dl = magnitude & 0xFFFFFFFFu;
    const uint64_t ih = inverse >> 32, il = inverse & 0xFFFFFFFFu;
    const uint64_t ah_bl = dh * il;
    const uint64_t al_bh = dl * ih;
    const uint64_t low_halves = dl * il;
    const uint64_t middle = (ah_bl & 0xFFFFFFFFu) +
                            (al_bh & 0xFFFFFFFFu) + (low_halves >> 32);
    uint64_t quotient = (dh * ih + (ah_bl >> 32) + (al_bh >> 32) +
                         (middle >> 32)) >> shift;
    if (!positive) quotient = uint64_t(0) - quotient;
    result.QuadPart = static_cast<LONGLONG>(quotient);
    return result;
}

extern "C" LONG __stdcall K_RtlCharToInteger(const char* text, ULONG base, ULONG* value) {
    if (!text || !value) return LONG(0xC000000Du);
    return ParseUnsigned(text, base, value) ? 0 : LONG(0xC000000Du);
}
extern "C" LONG __stdcall K_RtlUnicodeStringToInteger(const GuestUnicodeString* text, ULONG base, ULONG* value) {
    if (!text || !value || (!text->Buffer && text->Length) || (text->Length & 1))
        return LONG(0xC000000Du);
    return ParseUnsigned(text->Buffer, text->Length / sizeof(wchar_t), base, value)
        ? 0 : LONG(0xC000000Du);
}
extern "C" LONG __stdcall K_RtlIntegerToChar(ULONG value, ULONG base, LONG length, char* output) {
    if (!output || length <= 0 || base < 2 || base > 36) return LONG(0xC000000Du);
    const std::string formatted = FormatUnsigned(value, base);
    if (formatted.size() + 1 > static_cast<size_t>(length)) return LONG(0xC0000023u);
    std::memcpy(output, formatted.c_str(), formatted.size() + 1);
    return 0;
}
extern "C" LONG __stdcall K_RtlIntegerToUnicodeString(ULONG value, ULONG base, GuestUnicodeString* output) {
    if (!output || !output->Buffer || (base != 0 && (base < 2 || base > 36)))
        return LONG(0xC000000Du);
    const std::string formatted = FormatUnsigned(value, base ? base : 10);
    const size_t bytes = (formatted.size() + 1) * sizeof(wchar_t);
    if (bytes > output->MaximumLength || formatted.size() * sizeof(wchar_t) > 0xFFFF)
        return LONG(0xC0000023u);
    for (size_t i = 0; i < formatted.size(); ++i) output->Buffer[i] = wchar_t(formatted[i]);
    output->Buffer[formatted.size()] = L'\0';
    output->Length = USHORT(formatted.size() * sizeof(wchar_t));
    return 0;
}
extern "C" LONG __stdcall K_RtlMultiByteToUnicodeN(wchar_t* output, ULONG maxBytes, ULONG* bytesWritten,
                                                    const char* input, ULONG inputBytes) {
    if (bytesWritten) *bytesWritten = 0;
    if ((!output && maxBytes) || (!input && inputBytes) || (maxBytes % sizeof(wchar_t)))
        return LONG(0xC000000Du);
    if (!inputBytes) return 0;
    int chars = MultiByteToWideChar(CP_ACP, 0, input, static_cast<int>(inputBytes), nullptr, 0);
    if (chars <= 0) return LONG(0xC000000Du);
    ULONG capacity = maxBytes / sizeof(wchar_t);
    int count = MultiByteToWideChar(CP_ACP, 0, input, static_cast<int>(inputBytes),
                                    output, static_cast<int>(capacity));
    if (count <= 0) return LONG(0xC0000023u);
    if (bytesWritten) *bytesWritten = ULONG(count) * sizeof(wchar_t);
    return 0;
}
extern "C" LONG __stdcall K_RtlUnicodeToMultiByteN(char* output, ULONG maxBytes, ULONG* bytesWritten,
                                                    const wchar_t* input, ULONG inputBytes) {
    if (bytesWritten) *bytesWritten = 0;
    if ((!output && maxBytes) || (!input && inputBytes) || (inputBytes % sizeof(wchar_t)))
        return LONG(0xC000000Du);
    if (!inputBytes) return 0;
    int chars = WideCharToMultiByte(CP_ACP, 0, input, static_cast<int>(inputBytes / sizeof(wchar_t)),
                                    nullptr, 0, nullptr, nullptr);
    if (chars <= 0) return LONG(0xC000000Du);
    int count = WideCharToMultiByte(CP_ACP, 0, input, static_cast<int>(inputBytes / sizeof(wchar_t)),
                                    output, static_cast<int>(maxBytes), nullptr, nullptr);
    if (count <= 0) return LONG(0xC0000023u);
    if (bytesWritten) *bytesWritten = ULONG(count);
    return 0;
}

extern "C" LONG __stdcall K_RtlMultiByteToUnicodeSize(ULONG* bytes, const char* input, ULONG inputBytes) {
    if (!bytes || (!input && inputBytes)) return LONG(0xC000000Du);
    *bytes = 0;
    if (!inputBytes) return 0;
    if (inputBytes > ULONG(INT_MAX)) return LONG(0xC000000Du);
    const int chars = MultiByteToWideChar(CP_ACP, 0, input, static_cast<int>(inputBytes), nullptr, 0);
    if (chars <= 0 || uint64_t(chars) * sizeof(wchar_t) > 0xFFFFFFFFu) return LONG(0xC000000Du);
    *bytes = ULONG(chars) * sizeof(wchar_t);
    return 0;
}
extern "C" LONG __stdcall K_RtlUnicodeToMultiByteSize(ULONG* bytes, const wchar_t* input, ULONG inputBytes) {
    if (!bytes || (!input && inputBytes) || (inputBytes & 1)) return LONG(0xC000000Du);
    *bytes = 0;
    if (!inputBytes) return 0;
    if (inputBytes / sizeof(wchar_t) > ULONG(INT_MAX)) return LONG(0xC000000Du);
    const int count = WideCharToMultiByte(CP_ACP, 0, input, static_cast<int>(inputBytes / sizeof(wchar_t)),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return LONG(0xC000000Du);
    *bytes = ULONG(count);
    return 0;
}

extern "C" LONG __stdcall K_RtlAnsiStringToUnicodeString(GuestUnicodeString* d,const GuestAnsiString* s,BOOL allocate){
    if(!d||!s||(!s->Buffer&&s->Length))return LONG(0xC000000Du);
    int chars=s->Length?MultiByteToWideChar(CP_ACP,0,s->Buffer,s->Length,nullptr,0):0;
    if(s->Length&&chars<=0)return LONG(0xC000000Du);
    uint32_t bytes=uint32_t(chars)*sizeof(wchar_t), required=bytes+sizeof(wchar_t);
    if(allocate){d->Buffer=static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,required));if(!d->Buffer)return LONG(0xC0000017u);TrackStringAllocation(d->Buffer);d->MaximumLength=USHORT(required>0xFFFF?0xFFFF:required);}
    else if(!d->Buffer||d->MaximumLength<bytes)return LONG(0xC0000023u);
    if(chars&&MultiByteToWideChar(CP_ACP,0,s->Buffer,s->Length,d->Buffer,chars)!=chars)return LONG(0xC000000Du);
    d->Length=USHORT(bytes);if(d->MaximumLength>d->Length)d->Buffer[chars]=0;return 0;
}
extern "C" LONG __stdcall K_RtlUnicodeStringToAnsiString(GuestAnsiString* d,const GuestUnicodeString* s,BOOL allocate){
    if(!d||!s||(!s->Buffer&&s->Length))return LONG(0xC000000Du);
    int bytes=s->Length?WideCharToMultiByte(CP_ACP,0,s->Buffer,s->Length/sizeof(wchar_t),nullptr,0,nullptr,nullptr):0;
    if(s->Length&&bytes<=0)return LONG(0xC000000Du);
    uint32_t required=uint32_t(bytes)+1;
    if(allocate){d->Buffer=static_cast<char*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,required));if(!d->Buffer)return LONG(0xC0000017u);TrackStringAllocation(d->Buffer);d->MaximumLength=USHORT(required>0xFFFF?0xFFFF:required);}
    else if(!d->Buffer||d->MaximumLength<uint32_t(bytes))return LONG(0xC0000023u);
    if(bytes&&WideCharToMultiByte(CP_ACP,0,s->Buffer,s->Length/sizeof(wchar_t),d->Buffer,bytes,nullptr,nullptr)!=bytes)return LONG(0xC000000Du);
    d->Length=USHORT(bytes);if(d->MaximumLength>d->Length)d->Buffer[bytes]=0;return 0;
}
extern "C" void __stdcall K_RtlFreeAnsiString(GuestAnsiString* s){if(s&&s->Buffer&&FreeStringAllocation(s->Buffer)){s->Buffer=nullptr;s->Length=s->MaximumLength=0;}}
extern "C" void __stdcall K_RtlFreeUnicodeString(GuestUnicodeString* s){if(s&&s->Buffer&&FreeStringAllocation(s->Buffer)){s->Buffer=nullptr;s->Length=s->MaximumLength=0;}}

extern "C" BOOL __stdcall K_RtlCreateUnicodeString(GuestUnicodeString* destination, const wchar_t* source) {
    if (!destination || !source) return FALSE;
    const size_t chars = wcslen(source);
    if (chars > 0x7FFE) return FALSE;
    const size_t bytes = (chars + 1) * sizeof(wchar_t);
    wchar_t* buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
    if (!buffer) return FALSE;
    std::memcpy(buffer, source, chars * sizeof(wchar_t));
    TrackStringAllocation(buffer);
    destination->Buffer = buffer;
    destination->Length = USHORT(chars * sizeof(wchar_t));
    destination->MaximumLength = USHORT(bytes);
    return TRUE;
}
extern "C" void __stdcall K_RtlUpperString(GuestAnsiString* destination, const GuestAnsiString* source) {
    if (!destination || !source || !destination->Buffer || !source->Buffer) return;
    const USHORT count = std::min(source->Length, destination->MaximumLength);
    for (USHORT i = 0; i < count; ++i)
        destination->Buffer[i] = char(std::toupper(static_cast<unsigned char>(source->Buffer[i])));
    destination->Length = count;
}
extern "C" ULONG __stdcall K_RtlCompareMemory(const void* a, const void* b, ULONG length) {
    if (!a || !b) return 0;
    const auto* x = static_cast<const uint8_t*>(a);
    const auto* y = static_cast<const uint8_t*>(b);
    ULONG matched = 0;
    while (matched < length && x[matched] == y[matched]) ++matched;
    return matched;
}
extern "C" LONG __stdcall K_RtlUpcaseUnicodeToMultiByteN(char* output, ULONG maxBytes, ULONG* bytesWritten,
                                                           const wchar_t* input, ULONG inputBytes) {
    if (bytesWritten) *bytesWritten = 0;
    if ((!output && maxBytes) || (!input && inputBytes) || (inputBytes & 1))
        return LONG(0xC000000Du);
    const ULONG chars = inputBytes / sizeof(wchar_t);
    std::wstring upper;
    upper.resize(chars);
    for (ULONG i = 0; i < chars; ++i) upper[i] = wchar_t(towupper(input[i]));
    return K_RtlUnicodeToMultiByteN(output, maxBytes, bytesWritten, upper.data(), inputBytes);
}
extern "C" uint32_t __stdcall K_RtlCompareMemoryUlong(const void* p,uint32_t length,uint32_t pattern){
    if(!p)return 0;const uint8_t* b=static_cast<const uint8_t*>(p);uint32_t matched=0;
    while(matched+4<=length){uint32_t v;std::memcpy(&v,b+matched,4);if(v!=pattern)break;matched+=4;}return matched;
}
extern "C" void __stdcall K_RtlFillMemoryUlong(void* p,uint32_t length,uint32_t pattern){
    if(!p)return;uint8_t* b=static_cast<uint8_t*>(p);for(uint32_t i=0;i+4<=length;i+=4)std::memcpy(b+i,&pattern,4);
}
extern "C" void __stdcall K_RtlMapGenericMask(ACCESS_MASK* access,const GENERIC_MAPPING* mapping){
    if(!access||!mapping)return;ACCESS_MASK v=*access;v&=~(GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE|GENERIC_ALL);
    if(*access&GENERIC_READ)v|=mapping->GenericRead;if(*access&GENERIC_WRITE)v|=mapping->GenericWrite;
    if(*access&GENERIC_EXECUTE)v|=mapping->GenericExecute;if(*access&GENERIC_ALL)v|=mapping->GenericAll;*access=v;
}
extern "C" uint32_t __stdcall K_RtlNtStatusToDosError(LONG status){
    using Fn=ULONG (WINAPI*)(LONG);static Fn fn=[](){HMODULE n=GetModuleHandleW(L"ntdll.dll");return n?reinterpret_cast<Fn>(GetProcAddress(n,"RtlNtStatusToDosError")):nullptr;}();
    if(fn)return fn(status);
    switch(uint32_t(status)){case 0:return ERROR_SUCCESS;case 0xC000000Du:return ERROR_INVALID_PARAMETER;case 0xC0000008u:return ERROR_INVALID_HANDLE;case 0xC000000Fu:case 0xC0000034u:return ERROR_FILE_NOT_FOUND;case 0xC0000022u:return ERROR_ACCESS_DENIED;case 0xC0000017u:return ERROR_NOT_ENOUGH_MEMORY;case 0xC000003Au:return ERROR_PATH_NOT_FOUND;default:return ERROR_MR_MID_NOT_FOUND;}
}
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
static LONG RtlCaseUnicodeString(GuestUnicodeString* destination, const GuestUnicodeString* source, BOOL allocate, bool upper) {
    if (!destination || !source || (!source->Buffer && source->Length) || (source->Length & 1))
        return LONG(0xC000000Du);
    const ULONG bytes = source->Length;
    if (allocate) {
        if (bytes > 0xFFFD) return LONG(0xC0000023u);
        wchar_t* buffer = static_cast<wchar_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(wchar_t)));
        if (!buffer) return LONG(0xC0000017u);
        destination->Buffer = buffer;
        TrackStringAllocation(buffer);
        destination->MaximumLength = USHORT(bytes + sizeof(wchar_t));
    } else if (!destination->Buffer || destination->MaximumLength < bytes) {
        return LONG(0xC0000023u);
    }
    const ULONG chars = bytes / sizeof(wchar_t);
    for (ULONG i = 0; i < chars; ++i) {
        const wchar_t c = source->Buffer[i];
        if (upper) destination->Buffer[i] = (c >= L'a' && c <= L'z') ? wchar_t(c - 32) : wchar_t(towupper(c));
        else destination->Buffer[i] = (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : wchar_t(towlower(c));
    }
    if (destination->MaximumLength >= bytes + sizeof(wchar_t)) destination->Buffer[chars] = L'\0';
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
extern "C" LONG __stdcall K_NtCreateSemaphore(HANDLE* out, ACCESS_MASK, void*, LONG initial, LONG maximum){
    if(!out||maximum<=0||initial<0||initial>maximum)return LONG(0xC000000Du);
    HANDLE h=CreateSemaphoreW(nullptr,initial,maximum,nullptr);if(!h)return LONG(0xC0000017u);*out=h;return 0;
}

using NtQueryObjectInfoFn = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
static FARPROC ResolveNtQueryRoutine(const char* name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : nullptr;
}
static LONG QueryNativeObject(HANDLE handle, ULONG infoClass, void* info,
                              ULONG length, ULONG* returnLength, const char* routine) {
    if (!handle || handle == INVALID_HANDLE_VALUE) return LONG(0xC0000008u);
    FARPROC address = ResolveNtQueryRoutine(routine);
    if (!address) return LONG(0xC0000002u); // STATUS_NOT_IMPLEMENTED
    auto query = reinterpret_cast<NtQueryObjectInfoFn>(address);
    return query(handle, infoClass, info, length, returnLength);
}
extern "C" LONG __stdcall K_NtQueryEvent(HANDLE event, ULONG infoClass, void* info, ULONG length, ULONG* returnLength) {
    return QueryNativeObject(event, infoClass, info, length, returnLength, "NtQueryEvent");
}
extern "C" LONG __stdcall K_NtQueryMutant(HANDLE mutant, ULONG infoClass, void* info, ULONG length, ULONG* returnLength) {
    return QueryNativeObject(mutant, infoClass, info, length, returnLength, "NtQueryMutant");
}
extern "C" LONG __stdcall K_NtQuerySemaphore(HANDLE semaphore, ULONG infoClass, void* info, ULONG length, ULONG* returnLength) {
    return QueryNativeObject(semaphore, infoClass, info, length, returnLength, "NtQuerySemaphore");
}
extern "C" LONG __stdcall K_NtQueryTimer(HANDLE timer, ULONG infoClass, void* info, ULONG length, ULONG* returnLength) {
    return QueryNativeObject(timer, infoClass, info, length, returnLength, "NtQueryTimer");
}
extern "C" LONG __stdcall K_NtReleaseSemaphore(HANDLE h,LONG count,LONG* previous){
    if(count<=0)return LONG(0xC000000Du);LONG p=0;BOOL ok=ReleaseSemaphore(h,count,&p);if(ok&&previous)*previous=p;return ok?0:LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtWaitForMultipleObjects(ULONG count, const HANDLE* handles, ULONG waitType, BOOL alertable, const LARGE_INTEGER* timeout, void*) {
    if (!handles || !count || count > MAXIMUM_WAIT_OBJECTS || waitType > 1) return LONG(0xC000000Du);
    for (ULONG i = 0; i < count; ++i)
        if (!handles[i] || handles[i] == INVALID_HANDLE_VALUE) return LONG(0xC0000008u);
    const DWORD result = WaitForMultipleObjectsEx(count, handles, waitType == 0,
                                                   KernelTimeout(timeout), alertable ? TRUE : FALSE);
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count)
        return waitType == 0 ? LONG(result - WAIT_OBJECT_0) : 0;
    if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) return LONG(0x80);
    if (result == WAIT_TIMEOUT) return LONG(0x00000102u);
    if (result == WAIT_IO_COMPLETION) return LONG(0x000000C0u);
    return LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtSignalAndWaitForSingleObject(HANDLE signal, HANDLE wait, BOOL alertable, const LARGE_INTEGER* timeout) {
    if (!signal || !wait || signal == INVALID_HANDLE_VALUE || wait == INVALID_HANDLE_VALUE)
        return LONG(0xC0000008u);
    const DWORD result = SignalObjectAndWait(signal, wait, KernelTimeout(timeout), alertable ? TRUE : FALSE);
    if (result == WAIT_OBJECT_0) return 0;
    if (result == WAIT_TIMEOUT) return LONG(0x00000102u);
    if (result == WAIT_IO_COMPLETION) return LONG(0x000000C0u);
    return LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtFreeVirtualMemory(void** base, SIZE_T* size, ULONG type) {
    if (!base || !*base) return LONG(0xC000000Du);
    SIZE_T n = size ? *size : 0;
    if (!VirtualFree(*base, n, type)) return LONG(0xC000000Du);
    if (type & MEM_RELEASE) { *base = nullptr; if (size) *size = 0; }
    return 0;
}


struct GuestClientId { HANDLE UniqueProcess; HANDLE UniqueThread; };
extern "C" LONG __stdcall K_PsCreateSystemThread(HANDLE* threadHandle, ULONG, void*, HANDLE process,
                                                   GuestClientId* clientId, LPTHREAD_START_ROUTINE start,
                                                   void* context) {
    if (!threadHandle || !start) return LONG(0xC000000Du);
    if (process && process != GetCurrentProcess() && process != reinterpret_cast<HANDLE>(intptr_t(-1)))
        return LONG(0xC000000Du);
    DWORD threadId = 0;
    HANDLE thread = CreateThread(nullptr, 0, start, context, 0, &threadId);
    if (!thread) return LONG(0xC0000017u);
    *threadHandle = thread;
    if (clientId) {
        clientId->UniqueProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(GetCurrentProcessId()));
        clientId->UniqueThread = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(threadId));
    }
    return 0;
}
extern "C" void __stdcall K_PsTerminateSystemThread(LONG status) { ExitThread(DWORD(status)); }
extern "C" LONG __stdcall K_KeSuspendThread(HANDLE thread) {
    DWORD previous = SuspendThread(thread);
    return previous == DWORD(-1) ? LONG(0xC0000008u) : LONG(previous);
}
extern "C" LONG __stdcall K_KeResumeThread(HANDLE thread) {
    DWORD previous = ResumeThread(thread);
    return previous == DWORD(-1) ? LONG(0xC0000008u) : LONG(previous);
}
extern "C" LONG __stdcall K_KeAlertResumeThread(HANDLE thread) { return K_KeResumeThread(thread); }
extern "C" LONG __stdcall K_KeSetPriorityThread(HANDLE thread, LONG priority) {
    const int previous = GetThreadPriority(thread);
    if (previous == THREAD_PRIORITY_ERROR_RETURN) return LONG(0xC0000008u);
    const int mapped = priority >= 15 ? THREAD_PRIORITY_TIME_CRITICAL :
                       priority >= 8 ? THREAD_PRIORITY_HIGHEST :
                       priority >= 0 ? THREAD_PRIORITY_NORMAL :
                       priority <= -15 ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_BELOW_NORMAL;
    return SetThreadPriority(thread, mapped) ? previous : LONG(0xC000000Du);
}
extern "C" LONG __stdcall K_KeSetBasePriorityThread(HANDLE thread, LONG increment) {
    return K_KeSetPriorityThread(thread, increment);
}
extern "C" LONG __stdcall K_KeQueryBasePriorityThread(HANDLE) { return 0; }
extern "C" void __stdcall K_KeSetPriorityProcess(HANDLE process, LONG priority) {
    const DWORD cls = priority > 0 ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    if (process) SetPriorityClass(process, cls);
}
extern "C" void __stdcall K_KeSetDisableBoostThread(HANDLE thread, BOOL disable) {
    if (thread) SetThreadPriorityBoost(thread, disable);
}
extern "C" void __stdcall K_KeBoostPriorityThread(HANDLE) { /* Windows schedules runnable threads. */ }
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

extern "C" UCHAR __fastcall K_KfRaiseIrql(UCHAR level) {
    const UCHAR previous = UCHAR(g_guestIrql);
    if (level > g_guestIrql) g_guestIrql = level;
    return previous;
}
extern "C" void __fastcall K_KfLowerIrql(UCHAR level) { g_guestIrql = level; }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToDpcLevel() { return K_KfRaiseIrql(2); }
extern "C" UCHAR __stdcall K_KeRaiseIrqlToSynchLevel() { return K_KfRaiseIrql(1); }

extern "C" void __stdcall K_KeInitializeDpc(void* dpc, GuestDpcInfo::Routine routine, void* context) {
    if (!dpc || !routine) return;
    auto info = std::make_unique<GuestDpcInfo>();
    info->routine = routine;
    info->context = context;
    std::lock_guard<std::mutex> lock(g_dpcMutex);
    g_dpcs[dpc] = std::move(info);
}
extern "C" BOOL __stdcall K_KeInsertQueueDpc(void* dpc, void* arg1, void* arg2) {
    GuestDpcInfo* info = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dpcMutex);
        auto it = g_dpcs.find(dpc);
        if (it == g_dpcs.end()) return FALSE;
        info = it->second.get();
        if (info->pending.exchange(true)) return FALSE;
    }
    auto work = std::make_unique<GuestDpcWork>();
    work->info = info; work->dpc = dpc; work->arg1 = arg1; work->arg2 = arg2;
    HANDLE thread = CreateThread(nullptr, 0, GuestDpcWorker, work.get(), 0, nullptr);
    if (!thread) { info->pending = false; return FALSE; }
    work.release();
    CloseHandle(thread);
    return TRUE;
}
extern "C" BOOL __stdcall K_KeRemoveQueueDpc(void* dpc) {
    std::lock_guard<std::mutex> lock(g_dpcMutex);
    auto it = g_dpcs.find(dpc);
    return it != g_dpcs.end() && it->second->pending.exchange(false);
}
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

extern "C" int __cdecl K_RtlSnprintf(char* output, size_t capacity, const char* format, ...) {
    if (!output || !capacity || !format) return -1;
    va_list args; va_start(args, format);
    const int result = vsnprintf(output, capacity, format, args);
    va_end(args);
    return result;
}
extern "C" int __cdecl K_RtlSprintf(char* output, const char* format, ...) {
    if (!output || !format) return -1;
    va_list args; va_start(args, format);
    const int result = vsprintf(output, format, args);
    va_end(args);
    return result;
}
extern "C" int __cdecl K_RtlVsnprintf(char* output, size_t capacity, const char* format, va_list args) {
    if (!output || !capacity || !format) return -1;
    return vsnprintf(output, capacity, format, args);
}
extern "C" int __cdecl K_RtlVsprintf(char* output, const char* format, va_list args) {
    if (!output || !format) return -1;
    return vsprintf(output, format, args);
}

extern "C" BOOL __stdcall K_KeIsExecutingDpc() { return g_insideDpc ? TRUE : FALSE; }
extern "C" BOOL __stdcall K_KeTestAlertThread(ULONG) { return FALSE; }
extern "C" LONG __stdcall K_KeSetEventBoostPriority(void* event, LONG increment, BOOL wait) {
    return K_KeSetEvent(event, increment, wait);
}
extern "C" ULONG __stdcall K_RtlWalkFrameChain(void** frames, ULONG count, ULONG) {
    if (!frames || !count) return 0;
    return ULONG(CaptureStackBackTrace(1, count, frames, nullptr));
}
extern "C" ULONG __stdcall K_RtlCaptureStackBackTrace(ULONG skip, ULONG count, void** frames, ULONG* hash) {
    if (!frames || !count) return 0;
    return ULONG(CaptureStackBackTrace(skip + 1, count, frames, hash));
}
extern "C" void __stdcall K_RtlGetCallersAddress(void** caller, void** callersCaller) {
    void* frames[2]{};
    CaptureStackBackTrace(1, 2, frames, nullptr);
    if (caller) *caller = frames[0];
    if (callersCaller) *callersCaller = frames[1];
}
extern "C" void __stdcall K_RtlAssert(const char* expression, const char* file, ULONG line, const char* message) {
    char buffer[512]{};
    sprintf_s(buffer, "[XboxRuntime] ASSERT: %s (%s:%lu) %s\\n",
              expression ? expression : "?", file ? file : "?", line, message ? message : "");
    OutputDebugStringA(buffer);
    DebugBreak();
}
extern "C" void __stdcall K_RtlRip(const char* message, const char* file, ULONG line) {
    char buffer[512]{};
    sprintf_s(buffer, "[XboxRuntime] RIP: %s (%s:%lu)\\n",
              message ? message : "", file ? file : "?", line);
    OutputDebugStringA(buffer);
    DebugBreak();
}
extern "C" void __stdcall K_KeBugCheck(ULONG code) { ExitProcess(code); }
extern "C" void __stdcall K_KeBugCheckEx(ULONG code, ULONG, ULONG, ULONG, ULONG) { ExitProcess(code); }
extern "C" void __stdcall K_HalReturnToFirmware(ULONG) { ExitProcess(0); }
extern "C" BOOL __stdcall K_HalIsResetOrShutdownPending() { return FALSE; }
extern "C" void __stdcall K_HalInitiateShutdown() { ExitProcess(0); }
extern "C" ULONG __stdcall K_DbgPrompt(const char* prompt, char* response, ULONG length) {
    if (prompt) OutputDebugStringA(prompt);
    if (response && length) response[0] = 0;
    return 0;
}
extern "C" void __stdcall K_DbgLoadImageSymbols(const char*, void*, ULONG) {}
extern "C" void __stdcall K_DbgUnLoadImageSymbols(const char*, void*, ULONG) {}

extern "C" ULONG __stdcall K_FscGetCacheSize() { return g_fileCacheSize; }
extern "C" void __stdcall K_FscSetCacheSize(ULONG bytes) { g_fileCacheSize = bytes; }
extern "C" void __stdcall K_FscInvalidateIdleBlocks() { /* Host filesystem owns its cache. */ }
extern "C" ULONG __stdcall K_HalReadSMCTrayState() { return 0; }
extern "C" void __stdcall K_ExRaiseStatus(LONG status) { RaiseException(DWORD(status), 0, 0, nullptr); }
extern "C" void __stdcall K_RtlRaiseStatus(LONG status) { RaiseException(DWORD(status), 0, 0, nullptr); }
extern "C" void __stdcall K_ExRaiseException(EXCEPTION_RECORD* record) {
    if (record) RaiseException(record->ExceptionCode, record->ExceptionFlags, record->NumberParameters,
                               reinterpret_cast<const ULONG_PTR*>(record->ExceptionInformation));
}
extern "C" void __stdcall K_RtlRaiseException(EXCEPTION_RECORD* record) {
    K_ExRaiseException(record);
}
extern "C" LONG __stdcall K_NtPulseEvent(HANDLE event, LONG* previous) {
    if (previous) *previous = WaitForSingleObject(event, 0) == WAIT_OBJECT_0 ? 1 : 0;
    return PulseEvent(event) ? 0 : LONG(0xC0000008u);
}
extern "C" LONG __stdcall K_NtCancelTimer(HANDLE timer, BOOL* previous) {
    if (previous) *previous = FALSE;
    if (!timer) return LONG(0xC0000008u);
    const BOOL ok = CancelWaitableTimer(timer);
    if (previous) *previous = ok;
    return ok ? 0 : LONG(0xC0000008u);
}

static FARPROC ResolveNtQueryRoutine(const char* name);
using NtSetTimerFn = LONG (NTAPI*)(HANDLE, const LARGE_INTEGER*, void*, void*, BOOLEAN, LONG, BOOLEAN*);
extern "C" LONG __stdcall K_NtSetTimerEx(
    HANDLE timer, const LARGE_INTEGER* dueTime, void* apcRoutine, ULONG apcMode,
    void* apcContext, BOOLEAN wakeTimer, LONG period, BOOLEAN* previousState) {
    if (previousState) *previousState = FALSE;
    if (!timer || timer == INVALID_HANDLE_VALUE || !dueTime || period < 0)
        return LONG(0xC000000Du);
    // Xbox APC dispatch/context translation is not implemented. Do not pass an
    // Xbox guest callback into the host process as though it were a host pointer.
    if (apcRoutine || apcContext) return LONG(0xC00000BBu);
    FARPROC address = ResolveNtQueryRoutine("NtSetTimer");
    if (!address) return LONG(0xC0000002u);
    auto setTimer = reinterpret_cast<NtSetTimerFn>(address);
    BOOLEAN oldState = FALSE;
    const LONG status = setTimer(timer, dueTime, nullptr, nullptr,
                                 wakeTimer, period, &oldState);
    if (previousState) *previousState = oldState;
    (void)apcMode;
    return status;
}

extern "C" LONG __stdcall K_NtCreateMutant(HANDLE* out, ACCESS_MASK, void*, BOOL initialOwner) {
    if (!out) return LONG(0xC000000Du);
    HANDLE h = CreateMutexW(nullptr, initialOwner, nullptr);
    if (!h) return LONG(0xC0000017u);
    *out = h; return 0;
}
extern "C" LONG __stdcall K_NtReleaseMutant(HANDLE mutant, LONG* previous) {
    if (previous) *previous = 0;
    if (!mutant) return LONG(0xC0000008u);
    if (!ReleaseMutex(mutant)) return LONG(0xC0000008u);
    if (previous) *previous = 1;
    return 0;
}
extern "C" LONG __stdcall K_NtCreateTimer(HANDLE* out, ACCESS_MASK, void*, ULONG type) {
    if (!out) return LONG(0xC000000Du);
    HANDLE h = CreateWaitableTimerW(nullptr, type == 0, nullptr);
    if (!h) return LONG(0xC0000017u);
    *out = h; return 0;
}
extern "C" LONG __stdcall K_NtSuspendThread(HANDLE thread, ULONG* previous) {
    DWORD n = SuspendThread(thread);
    if (n == DWORD(-1)) return LONG(0xC0000008u);
    if (previous) *previous = n;
    return 0;
}
extern "C" LONG __stdcall K_NtResumeThread(HANDLE thread, ULONG* previous) {
    DWORD n = ResumeThread(thread);
    if (n == DWORD(-1)) return LONG(0xC0000008u);
    if (previous) *previous = n;
    return 0;
}
extern "C" LONG __stdcall K_NtDuplicateObject(HANDLE sourceProcess, HANDLE source, HANDLE targetProcess,
                                                HANDLE* target, ACCESS_MASK access, ULONG attributes, ULONG options) {
    if (!target) return LONG(0xC000000Du);
    HANDLE process = targetProcess ? targetProcess : GetCurrentProcess();
    if (!DuplicateHandle(sourceProcess ? sourceProcess : GetCurrentProcess(), source, process, target,
                         access, (attributes & 2u) != 0, options))
        return LONG(0xC0000008u);
    return 0;
}
extern "C" void __stdcall K_Unsupported(uint32_t ordinal){char b[128];sprintf_s(b,"[XboxKernel] Unsupported import ordinal %u; aborting guest thread.\n",ordinal);OutputDebugStringA(b);ULONG_PTR parameter=ordinal;RaiseException(0xC000001D,EXCEPTION_NONCONTINUABLE,1,&parameter);}
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
        cmp eax, 19
        je ex_interlocked_add_large_integer
        cmp eax, 20
        je ex_interlocked_add_large_statistic
        cmp eax, 21
        je ex_interlocked_compare_exchange64
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
        cmp eax, 74
        je io_invalid_device_request
        cmp eax, 361
        je rtl_snprintf
        cmp eax, 362
        je rtl_sprintf
        cmp eax, 363
        je rtl_vsnprintf
        cmp eax, 364
        je rtl_vsprintf
                cmp eax, 92
        je ke_alert_resume
        cmp eax, 94
        je ke_boost_priority
        cmp eax, 140
        je ke_resume_thread
        cmp eax, 143
        je ke_set_base_priority
        cmp eax, 144
        je ke_disable_boost
        cmp eax, 147
        je ke_set_process_priority
        cmp eax, 148
        je ke_set_thread_priority
        cmp eax, 152
        je ke_suspend_thread
        cmp eax, 254
        je ps_create_thread
        cmp eax, 258
        je ps_terminate_thread
        cmp eax, 56
        je interlocked_flush_slist
        cmp eax, 57
        je interlocked_pop_slist
        cmp eax, 58
        je interlocked_push_slist
        cmp eax, 32
        je exf_insert_head
        cmp eax, 33
        je exf_insert_tail
        cmp eax, 34
        je exf_remove_head
                cmp eax, 167
        je mm_alloc_system
        cmp eax, 172
        je mm_free_system
                cmp eax, 49
        je hal_return_firmware
        cmp eax, 265
        je rtl_capture_context
        cmp eax, 264
        je rtl_assert
                cmp eax, 9
        je hal_read_tray
        cmp eax, 26
        je ex_raise_exception
        cmp eax, 27
        je ex_raise_status
        cmp eax, 35
        je fsc_get_cache
        cmp eax, 36
        je fsc_invalidate
        cmp eax, 37
        je fsc_set_cache
        cmp eax, 192
        je nt_create_mutant
        cmp eax, 221
        je nt_release_mutant
                cmp eax, 185
        je nt_cancel_timer
        cmp eax, 192
        je nt_create_mutant
        cmp eax, 194
        je nt_create_timer
        cmp eax, 197
        je nt_duplicate_object
        cmp eax, 205
        je nt_pulse_event
        cmp eax, 224
        je nt_resume_thread
        cmp eax, 231
        je nt_suspend_thread
        cmp eax, 302
        je rtl_raise_exception
        cmp eax, 303
        je rtl_raise_status
                cmp eax, 7
        je dbg_load_symbols
        cmp eax, 10
        je dbg_prompt
        cmp eax, 11
        je dbg_unload_symbols
        cmp eax, 92
        je ke_alert_resume
        cmp eax, 95
        je ke_bugcheck
        cmp eax, 96
        je ke_bugcheck_ex
        cmp eax, 111
        je ke_init_queue
        cmp eax, 116
        je ke_insert_head_queue
        cmp eax, 117
        je ke_insert_queue
        cmp eax, 136
        je ke_remove_queue
        cmp eax, 106
        je ke_init_device_queue
        cmp eax, 114
        je ke_insert_by_key_device_queue
        cmp eax, 115
        je ke_insert_device_queue
        cmp eax, 133
        je ke_remove_by_key_device_queue
        cmp eax, 134
        je ke_remove_device_queue
        cmp eax, 135
        je ke_remove_entry_device_queue
        cmp eax, 107
        je ke_init_dpc
        cmp eax, 119
        je ke_insert_dpc
        cmp eax, 137
        je ke_remove_dpc
                cmp eax, 121
        je ke_is_dpc
        cmp eax, 155
        je ke_test_alert
        cmp eax, 146
        je ke_set_event_boost
        cmp eax, 266
        je rtl_capture_stack
        cmp eax, 288
        je rtl_callers_address
        cmp eax, 319
        je rtl_walk_frame
        cmp eax, 352
        je rtl_rip
        cmp eax, 358
        je hal_reset_pending
        cmp eax, 360
        je hal_shutdown
                cmp eax, 124
        je ke_query_base_priority
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
        cmp eax, 218
        je nt_query_volume_information_file
        cmp eax, 229
        je nt_set_timer_ex
        cmp eax, 207
        je nt_query_directory_file
        cmp eax, 210
        je nt_query_full_attributes_file
        cmp eax, 211
        je nt_query_information_file
        cmp eax, 226
        je nt_set_information_file
        cmp eax, 190
        je nt_create_file
        cmp eax, 195
        je nt_delete_file
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
        cmp eax, 234
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
        cmp eax, 281
        je rtl_ext_mul
        cmp eax, 282
        je rtl_ext_div
        cmp eax, 283
        je rtl_ext_magic
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
        cmp eax, 209
        je nt_query_event
        cmp eax, 213
        je nt_query_mutant
        cmp eax, 214
        je nt_query_semaphore
        cmp eax, 216
        je nt_query_timer
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
    ex_interlocked_add_large_integer: jmp K_ExInterlockedAddLargeInteger
    ex_interlocked_add_large_statistic: jmp K_ExInterlockedAddLargeStatistic
    ex_interlocked_compare_exchange64: jmp K_ExInterlockedCompareExchange64
    pool_size: jmp K_ExQueryPoolBlockSize
    smbus_read: jmp K_HalReadSMBusValue
    smbus_write: jmp K_HalWriteSMBusValue
    icx: jmp K_InterlockedCompareExchange
    idec: jmp K_InterlockedDecrement
    iinc: jmp K_InterlockedIncrement
    iex: jmp K_InterlockedExchange
    iexa: jmp K_InterlockedExchangeAdd
    rtl_snprintf: jmp K_RtlSnprintf
    rtl_sprintf: jmp K_RtlSprintf
    rtl_vsnprintf: jmp K_RtlVsnprintf
    rtl_vsprintf: jmp K_RtlVsprintf
    ke_alert_resume: jmp K_KeAlertResumeThread
    ke_boost_priority: jmp K_KeBoostPriorityThread
    ke_resume_thread: jmp K_KeResumeThread
    ke_set_base_priority: jmp K_KeSetBasePriorityThread
    ke_disable_boost: jmp K_KeSetDisableBoostThread
    ke_set_process_priority: jmp K_KeSetPriorityProcess
    ke_set_thread_priority: jmp K_KeSetPriorityThread
    ke_suspend_thread: jmp K_KeSuspendThread
    ps_create_thread: jmp K_PsCreateSystemThread
    ps_terminate_thread: jmp K_PsTerminateSystemThread
    ke_query_base_priority: jmp K_KeQueryBasePriorityThread
    interlocked_flush_slist: jmp K_InterlockedFlushSList
    interlocked_pop_slist: jmp K_InterlockedPopEntrySList
    interlocked_push_slist: jmp K_InterlockedPushEntrySList
    exf_insert_head: jmp K_ExfInterlockedInsertHeadList
    exf_insert_tail: jmp K_ExfInterlockedInsertTailList
    exf_remove_head: jmp K_ExfInterlockedRemoveHeadList
    hal_return_firmware: jmp K_HalReturnToFirmware
    rtl_assert: jmp K_RtlAssert
    hal_read_tray: jmp K_HalReadSMCTrayState
    ex_raise_status: jmp K_ExRaiseStatus
    fsc_get_cache: jmp K_FscGetCacheSize
    fsc_invalidate: jmp K_FscInvalidateIdleBlocks
    fsc_set_cache: jmp K_FscSetCacheSize
    ex_raise_exception: jmp K_ExRaiseException
    nt_create_mutant: jmp K_NtCreateMutant
    nt_release_mutant: jmp K_NtReleaseMutant
    nt_cancel_timer: jmp K_NtCancelTimer
    nt_create_timer: jmp K_NtCreateTimer
    nt_duplicate_object: jmp K_NtDuplicateObject
    nt_pulse_event: jmp K_NtPulseEvent
    nt_resume_thread: jmp K_NtResumeThread
    nt_suspend_thread: jmp K_NtSuspendThread
    rtl_raise_exception: jmp K_RtlRaiseException
    rtl_raise_status: jmp K_RtlRaiseStatus
    dbg_load_symbols: jmp K_DbgLoadImageSymbols
    dbg_prompt: jmp K_DbgPrompt
    dbg_unload_symbols: jmp K_DbgUnLoadImageSymbols
    ke_bugcheck: jmp K_KeBugCheck
    ke_bugcheck_ex: jmp K_KeBugCheckEx
    ke_is_dpc: jmp K_KeIsExecutingDpc
    ke_test_alert: jmp K_KeTestAlertThread
    ke_set_event_boost: jmp K_KeSetEventBoostPriority
    io_invalid_device_request: jmp K_IoInvalidDeviceRequest
    rtl_capture_context: jmp K_RtlCaptureContext
    rtl_capture_stack: jmp K_RtlCaptureStackBackTrace
    rtl_callers_address: jmp K_RtlGetCallersAddress
    rtl_walk_frame: jmp K_RtlWalkFrameChain
    rtl_rip: jmp K_RtlRip
    hal_reset_pending: jmp K_HalIsResetOrShutdownPending
    hal_shutdown: jmp K_HalInitiateShutdown
    ex_acquire_rw_exclusive: jmp K_ExAcquireReadWriteLockExclusive
    ex_acquire_rw_shared: jmp K_ExAcquireReadWriteLockShared
    ex_init_rw: jmp K_ExInitializeReadWriteLock
    ex_release_rw: jmp K_ExReleaseReadWriteLock
    ke_init_queue: jmp K_KeInitializeQueue
    ke_insert_head_queue: jmp K_KeInsertHeadQueue
    ke_insert_queue: jmp K_KeInsertQueue
    ke_remove_queue: jmp K_KeRemoveQueue
    ke_init_device_queue: jmp K_KeInitializeDeviceQueue
    ke_insert_by_key_device_queue: jmp K_KeInsertByKeyDeviceQueue
    ke_insert_device_queue: jmp K_KeInsertDeviceQueue
    ke_remove_by_key_device_queue: jmp K_KeRemoveByKeyDeviceQueue
    ke_remove_device_queue: jmp K_KeRemoveDeviceQueue
    ke_remove_entry_device_queue: jmp K_KeRemoveEntryDeviceQueue
    ke_init_dpc: jmp K_KeInitializeDpc
    ke_insert_dpc: jmp K_KeInsertQueueDpc
    ke_remove_dpc: jmp K_KeRemoveQueueDpc
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
    mm_alloc_system: jmp K_MmAllocateSystemMemory
    mm_free_system: jmp K_MmFreeSystemMemory
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
    nt_query_volume_information_file: jmp K_NtQueryVolumeInformationFile
    nt_set_timer_ex: jmp K_NtSetTimerEx
    nt_query_directory_file: jmp K_NtQueryDirectoryFile
    nt_query_full_attributes_file: jmp K_NtQueryFullAttributesFile
    nt_query_information_file: jmp K_NtQueryInformationFile
    nt_set_information_file: jmp K_NtSetInformationFile
    nt_create_file: jmp K_NtCreateFile
    nt_delete_file: jmp K_NtDeleteFile
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
    rtl_ext_mul: jmp K_RtlExtendedIntegerMultiply
    rtl_ext_div: jmp K_RtlExtendedLargeIntegerDivide
    rtl_ext_magic: jmp K_RtlExtendedMagicDivide
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
    nt_query_event: jmp K_NtQueryEvent
    nt_query_mutant: jmp K_NtQueryMutant
    nt_query_semaphore: jmp K_NtQuerySemaphore
    nt_query_timer: jmp K_NtQueryTimer
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

void XboxKernelSetTitlePath(const wchar_t* path) {
    g_titleDirectory.clear();
    if (!path) return;
    g_titleDirectory = path;
    const size_t separator = g_titleDirectory.find_last_of(L"\\/");
    if (separator != std::wstring::npos) g_titleDirectory.resize(separator);
}

bool XboxKernelMountDvdImage(const wchar_t* path) {
    if (!path || !*path || g_dvdReady) return false;
    if (::ModuleStart() != 0) return false;
    if (XDVDFS_OpenImage(path, &g_dvdInfo) != 0) {
        ::ModuleStop();
        g_dvdInfo = {};
        return false;
    }
    g_dvdReady = true;
    OutputDebugStringW(L"[XboxKernel] XDVDFS disc image mounted in guest process.\\n");
    return true;
}

void XboxKernelUnmountDvdImage() {
    {
        std::lock_guard<std::mutex> lock(g_dvdFileMutex);
        for (const auto& item : g_dvdFiles) CloseHandle(item.first);
        g_dvdFiles.clear();
    }
    if (g_dvdReady) {
        XDVDFS_CloseImage();
        ::ModuleStop();
        g_dvdReady = false;
        g_dvdInfo = {};
    }
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
