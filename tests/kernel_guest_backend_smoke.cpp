#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../modules/xbe_loader/xbe_backend_api.h"

namespace {
using ModuleStartFn = int (__cdecl*)();
using ModuleStopFn = void (__cdecl*)();
using GetBackendFn = int (__cdecl*)(XBE_EXECUTION_BACKEND_V2*);

#pragma pack(push, 1)
struct TestSection {
    std::uint32_t flags;
    std::uint32_t virtualAddress;
    std::uint32_t virtualSize;
    std::uint32_t rawAddress;
    std::uint32_t rawSize;
    std::uint32_t nameAddress;
    std::uint32_t nameReferenceCount;
    std::uint32_t headSharedPageReferenceCountAddress;
    std::uint32_t tailSharedPageReferenceCountAddress;
    std::uint8_t digest[20];
};
#pragma pack(pop)
static_assert(sizeof(TestSection) == 56, "XBE section header layout");

#pragma pack(push, 4)
struct GuestListEntry32 { std::uint32_t Flink, Blink; };
struct GuestDispatcherHeader32 {
    std::uint8_t Type, Absolute, Size, Inserted;
    std::int32_t SignalState;
    GuestListEntry32 WaitListHead;
};
struct GuestEvent32 { GuestDispatcherHeader32 Header; };
struct GuestSemaphore32 { GuestDispatcherHeader32 Header; std::int32_t Limit; };
struct GuestMutant32 {
    GuestDispatcherHeader32 Header;
    GuestListEntry32 MutantListEntry;
    std::uint32_t OwnerThread;
    std::uint8_t Abandoned;
    std::uint8_t Padding[3];
};
struct GuestTimer32 {
    GuestDispatcherHeader32 Header;
    std::uint64_t DueTime;
    GuestListEntry32 TimerListEntry;
    std::uint32_t Dpc;
    std::int32_t Period;
};
struct GuestIoStatusBlock32 { std::int32_t Status; std::uint32_t Information; };
struct GuestIoCompletionBasicInformation32 { std::int32_t Depth; };
#pragma pack(pop)
static_assert(sizeof(GuestDispatcherHeader32) == 16, "NXDK DISPATCHER_HEADER");
static_assert(sizeof(GuestEvent32) == 16, "NXDK KEVENT");
static_assert(sizeof(GuestSemaphore32) == 20, "NXDK KSEMAPHORE");
static_assert(sizeof(GuestMutant32) == 32, "NXDK KMUTANT");
static_assert(sizeof(GuestTimer32) == 40, "NXDK KTIMER");
static_assert(sizeof(GuestIoStatusBlock32) == 8, "NXDK IO_STATUS_BLOCK");
static_assert(sizeof(GuestIoCompletionBasicInformation32) == 4, "NXDK IO_COMPLETION_BASIC_INFORMATION");

struct GuestCodeBuilder {
    std::uint8_t* bytes;
    std::size_t used = 0;
    void Emit8(std::uint8_t value) { bytes[used++] = value; }
    void Emit32(std::uint32_t value) {
        std::memcpy(bytes + used, &value, sizeof(value));
        used += sizeof(value);
    }
    void Push(std::uint32_t value) { Emit8(0x68); Emit32(value); }
    void PushMemory(std::uint32_t address) {
        Emit8(0xFF); Emit8(0x35); Emit32(address); // push dword ptr [absolute]
    }
    void CallIat(std::uint32_t address) {
        Emit8(0xFF); Emit8(0x15); Emit32(address); // call dword ptr [absolute]
    }
    void StoreEax(std::uint32_t address) {
        Emit8(0xA3); Emit32(address); // mov [absolute], eax
    }
};

int Fail(const char* message, int code, XBE_EXECUTION_BACKEND_V2* backend,
         std::uint8_t* image, ModuleStopFn stop) {
    std::fprintf(stderr, "kernel_guest_backend_smoke: %s (%d)\n", message, code);
    if (backend && backend->stopGuest) backend->stopGuest(backend->context);
    if (image && backend && backend->releaseImage)
        backend->releaseImage(image, backend->context);
    if (stop) stop();
    return code ? code : 1;
}
}

int main() {
    HMODULE module = LoadLibraryW(L"xbox_kernel.dll");
    if (!module) {
        std::fprintf(stderr, "LoadLibrary xbox_kernel.dll failed: %lu\n", GetLastError());
        return 1;
    }
    auto start = reinterpret_cast<ModuleStartFn>(
        GetProcAddress(module, "ModuleStart"));
    auto stop = reinterpret_cast<ModuleStopFn>(
        GetProcAddress(module, "ModuleStop"));
    auto getBackend = reinterpret_cast<GetBackendFn>(
        GetProcAddress(module, "XBE_GetExecutionBackendV2"));
    if (!start || !stop || !getBackend || start() != 0) {
        std::fprintf(stderr, "kernel module exports/startup failed\n");
        FreeLibrary(module);
        return 2;
    }

    XBE_EXECUTION_BACKEND_V2 backend{};
    backend.size = sizeof(backend);
    backend.version = XBE_EXECUTION_BACKEND_V2_VERSION;
    if (getBackend(&backend) != 0 ||
        backend.version != XBE_EXECUTION_BACKEND_V2_VERSION ||
        !backend.reserveImage || !backend.resolveImports ||
        !backend.startGuest || !backend.stopGuest || !backend.releaseImage) {
        stop();
        FreeLibrary(module);
        return 3;
    }

    constexpr std::uint32_t guestBase = 0x10000000u;
    constexpr std::uint32_t imageSize = 0x4000u;
    constexpr std::uint32_t headerSize = 0x1000u;
    constexpr std::uint32_t sectionTable = guestBase + 0x400u;
    constexpr std::uint32_t entryPoint = guestBase + 0x2000u;
    std::uint8_t* image = nullptr;
    const int reserve = backend.reserveImage(
        guestBase, imageSize, &image, backend.context);
    if (reserve != 0 || !image)
        return Fail("exact-base shared image reservation failed", reserve, &backend, image, stop);

    std::memset(image, 0, imageSize);
    std::memcpy(image + 0x120, &sectionTable, sizeof(sectionTable));
    const TestSection section{
        0x07u, entryPoint, 0x2000u, 0u, 0u, 0u, 0u, 0u, 0u, {}
    };
    std::memcpy(image + 0x400, &section, sizeof(section));
    image[0x2000] = 0xC3; // Minimal x86 guest entry: ret.

    // Build a real x86 TLS callback. On process attach it reads the TLS
    // template and zero-fill bytes through the guest thread's dynamic TlsSlots array.
    // On process detach it records that reverse-order cleanup ran.
    constexpr std::uint32_t tlsDirectoryOffset = 0x3000u;
    constexpr std::uint32_t tlsTemplateOffset = 0x3100u;
    constexpr std::uint32_t tlsCallbacksOffset = 0x3200u;
    constexpr std::uint32_t attachMarkerOffset = 0x3300u;
    constexpr std::uint32_t detachMarkerOffset = 0x3304u;
    constexpr std::uint32_t tlsCheckMarkerOffset = 0x3308u;
    constexpr std::uint32_t tlsIndexMarkerOffset = 0x330Cu;
    constexpr std::uint32_t tlsIndexVa = guestBase + tlsDirectoryOffset + 8u;
    std::uint32_t tlsDirectory[6] = {
        guestBase + tlsTemplateOffset,
        guestBase + tlsTemplateOffset + 4u,
        tlsIndexVa,
        guestBase + tlsCallbacksOffset,
        4u,
        0u
    };
    std::memcpy(image + tlsDirectoryOffset, tlsDirectory, sizeof(tlsDirectory));
    const std::uint32_t templateValue = 0x11223344u;
    std::memcpy(image + tlsTemplateOffset, &templateValue, sizeof(templateValue));
    const std::uint32_t callbackVa = guestBase + 0x3A00u;
    std::memcpy(image + tlsCallbacksOffset, &callbackVa, sizeof(callbackVa));

    std::uint8_t callback[128]{};
    std::size_t used = 0;
    auto emit8 = [&](std::uint8_t value) { callback[used++] = value; };
    auto emit32 = [&](std::uint32_t value) {
        std::memcpy(callback + used, &value, sizeof(value));
        used += sizeof(value);
    };
    auto emitStore = [&](std::uint32_t address, std::uint32_t value) {
        emit8(0xC7); emit8(0x05); emit32(address); emit32(value);
    };
    auto branch = [&]() {
        emit8(0x75); // jne rel8
        const std::size_t displacement = used;
        emit8(0);
        return displacement;
    };
    auto patchBranch = [&](std::size_t displacement, std::size_t target) {
        const int delta = static_cast<int>(target) -
                          static_cast<int>(displacement + 1u);
        if (delta < -128 || delta > 127) return false;
        callback[displacement] = static_cast<std::uint8_t>(
            static_cast<std::int8_t>(delta));
        return true;
    };

    emit8(0x83); emit8(0x7C); emit8(0x24); emit8(0x08); emit8(0x01); // cmp [esp+8],1
    const std::size_t toDetach = branch();
    emit8(0x8B); emit8(0x0D); emit32(tlsIndexVa);                 // mov ecx,[tlsIndex]
    emit8(0x64); emit8(0x8B); emit8(0x04); emit8(0x8D);
    emit32(0xE10u);                                                // mov eax,fs:[TlsSlots+ecx*4]
    emit8(0x89); emit8(0x0D); emit32(guestBase + tlsIndexMarkerOffset); // save allocated TLS index
    emit8(0x81); emit8(0x38); emit32(templateValue);              // cmp [eax],template
    const std::size_t badTemplate = branch();
    emit8(0x83); emit8(0x78); emit8(0x04); emit8(0x00);           // cmp [eax+4],0
    const std::size_t badZeroFill = branch();
    emitStore(guestBase + tlsCheckMarkerOffset, 1u);
    emit8(0xEB); const std::size_t skipFailure = used; emit8(0);
    const std::size_t failure = used;
    emitStore(guestBase + tlsCheckMarkerOffset, 0xBADu);
    const std::size_t attach = used;
    emitStore(guestBase + attachMarkerOffset, 1u);
    emit8(0xEB); const std::size_t skipDetach = used; emit8(0);
    const std::size_t detach = used;
    emitStore(guestBase + detachMarkerOffset, 1u);
    const std::size_t returnOffset = used;
    emit8(0xC2); emit8(0x0C); emit8(0x00);                       // ret 12
    if (!patchBranch(toDetach, detach) ||
        !patchBranch(badTemplate, failure) ||
        !patchBranch(badZeroFill, failure) ||
        !patchBranch(skipFailure, attach) ||
        !patchBranch(skipDetach, returnOffset))
        return Fail("TLS callback test stub branch overflow", 4, &backend, image, stop);
    std::memcpy(image + 0x3A00, callback, used);

    // Resolve real kernel ordinals to x86 guest-callable dispatch thunks, then
    // execute those thunks from the synthetic guest entrypoint. These calls
    // exercise the stdcall ABI and shared guest object layouts, not host helpers.
    constexpr std::uint32_t thunkTableOffset = 0x3400u;
    constexpr std::uint32_t eventOffset = 0x3500u;
    constexpr std::uint32_t semaphoreOffset = 0x3540u;
    constexpr std::uint32_t mutantOffset = 0x3580u;
    constexpr std::uint32_t timerOffset = 0x3600u;
    constexpr std::uint32_t markerOffset = 0x3800u;
    constexpr std::uint32_t timeoutOffset = 0x3740u;
    constexpr std::uint32_t dueTimeOffset = 0x3748u;
    constexpr std::uint32_t threadOutOffset = 0x3750u;
    constexpr std::uint32_t delayIntervalOffset = 0x3758u;
    constexpr std::uint32_t completionHandleOffset = 0x3760u;
    constexpr std::uint32_t duplicateHandleOffset = 0x3764u;
    constexpr std::uint32_t completionInfoOffset = 0x3770u;
    constexpr std::uint32_t completionKeyOffset = 0x3774u;
    constexpr std::uint32_t completionApcOffset = 0x3778u;
    constexpr std::uint32_t completionIoStatusOffset = 0x377Cu;
    constexpr std::uint32_t threadOutVa = guestBase + threadOutOffset;
    constexpr std::uint32_t delayIntervalVa = guestBase + delayIntervalOffset;
    constexpr std::uint32_t completionHandleVa = guestBase + completionHandleOffset;
    constexpr std::uint32_t duplicateHandleVa = guestBase + duplicateHandleOffset;
    constexpr std::uint32_t completionInfoVa = guestBase + completionInfoOffset;
    constexpr std::uint32_t completionKeyVa = guestBase + completionKeyOffset;
    constexpr std::uint32_t completionApcVa = guestBase + completionApcOffset;
    constexpr std::uint32_t completionIoStatusVa = guestBase + completionIoStatusOffset;
    constexpr std::uint32_t iat = guestBase + thunkTableOffset;
    constexpr std::uint32_t eventVa = guestBase + eventOffset;
    constexpr std::uint32_t semaphoreVa = guestBase + semaphoreOffset;
    constexpr std::uint32_t mutantVa = guestBase + mutantOffset;
    constexpr std::uint32_t timerVa = guestBase + timerOffset;
    constexpr std::uint32_t timeoutVa = guestBase + timeoutOffset;
    const std::uint32_t ordinals[] = {
        108u, 145u, 138u, 159u, 112u, 132u,
        110u, 131u, 113u, 149u, 150u, 97u, 146u, 99u,
        191u, 212u, 223u, 227u, 198u, 187u, 0u
    };
    for (std::size_t i = 0; i < sizeof(ordinals) / sizeof(ordinals[0]); ++i) {
        const std::uint32_t thunk = ordinals[i]
            ? (0x80000000u | ordinals[i]) : 0u;
        std::memcpy(image + thunkTableOffset + i * sizeof(thunk),
                    &thunk, sizeof(thunk));
    }
    const std::int64_t zeroTimeout = 0;
    const std::int64_t relativeDueTime = -100000; // 10 ms
    std::memcpy(image + timeoutOffset, &zeroTimeout, sizeof(zeroTimeout));
    std::memcpy(image + dueTimeOffset, &relativeDueTime, sizeof(relativeDueTime));
    const std::uint32_t invalidThreadPointer = 0xFFFFFFFFu;
    std::memcpy(image + threadOutOffset, &invalidThreadPointer, sizeof(invalidThreadPointer));
    const std::int64_t delayInterval = -10000; // 1 ms relative NT interval
    std::memcpy(image + delayIntervalOffset, &delayInterval, sizeof(delayInterval));

    GuestCodeBuilder guest{image + 0x2000};
    const auto push = [&](std::uint32_t value) { guest.Push(value); };
    const auto call = [&](std::uint32_t index) { guest.CallIat(iat + index * 4u); };
    const auto save = [&](std::uint32_t marker) {
        guest.StoreEax(guestBase + markerOffset + marker);
    };

    // Notification event: previous state 0 -> set, wait succeeds, reset
    // returns previous state 1, then a zero-time wait reports STATUS_TIMEOUT.
    push(0u); push(0u); push(eventVa); call(0u);
    push(0u); push(0u); push(eventVa); call(1u); save(0u);
    push(timeoutVa); push(0u); push(0u); push(0u); push(eventVa); call(3u); save(4u);
    push(eventVa); call(2u); save(8u);
    push(timeoutVa); push(0u); push(0u); push(0u); push(eventVa); call(3u); save(12u);
    push(threadOutVa); push(eventVa); call(12u); // KeSetEventBoostPriority(Event, PKTHREAD*)
    push(eventVa); call(2u); save(52u);

    // Semaphore: release returns prior count 0; one wait consumes the count;
    // a second zero-time wait times out.
    push(1u); push(0u); push(semaphoreVa); call(4u);
    push(0u); push(1u); push(0u); push(semaphoreVa); call(5u); save(16u);
    push(timeoutVa); push(0u); push(0u); push(0u); push(semaphoreVa); call(3u); save(20u);
    push(timeoutVa); push(0u); push(0u); push(0u); push(semaphoreVa); call(3u); save(24u);

    // Mutant: acquire by waiting and release from the owning guest thread.
    push(0u); push(mutantVa); call(6u);
    push(timeoutVa); push(0u); push(0u); push(0u); push(mutantVa); call(3u); save(28u);
    push(0u); push(0u); push(0u); push(mutantVa); call(7u); save(32u);

    // Timer: LARGE_INTEGER is passed by value (two stack DWORDs), as declared
    // by the pinned NXDK header. Both one-shot and periodic APIs are exercised.
    push(0u); push(timerVa); call(8u);
    push(0u); push(0u); push(static_cast<std::uint32_t>(relativeDueTime >> 32));
    push(static_cast<std::uint32_t>(relativeDueTime)); push(timerVa); call(9u); save(36u);
    push(timerVa); call(11u); save(40u);
    push(0u); push(5u); push(static_cast<std::uint32_t>(relativeDueTime >> 32));
    push(static_cast<std::uint32_t>(relativeDueTime)); push(timerVa); call(10u); save(44u);
    push(timerVa); call(11u); save(48u);
    push(delayIntervalVa); push(0u); push(0u); call(13u); save(56u);

    // I/O completion ports: create, duplicate in-process, close the original,
    // post/query/remove a packet through the duplicate, then verify timeout and
    // closed-handle behavior. The packet preserves both guest contexts,
    // NTSTATUS and Information.
    push(0u); push(0u); push(0x001F0003u); push(completionHandleVa); call(14u); save(60u);
    push(0u); push(0u); push(0u); push(0xFFFFFFFFu);
    push(duplicateHandleVa); guest.PushMemory(completionHandleVa);
    push(0xFFFFFFFFu); call(18u); save(64u);
    guest.PushMemory(completionHandleVa); call(19u); save(68u);
    push(0x1234u); push(0xC0000001u); push(0x33334444u); push(0x11112222u);
    guest.PushMemory(duplicateHandleVa); call(17u); save(72u);
    push(completionInfoVa); guest.PushMemory(duplicateHandleVa); call(15u); save(76u);
    push(timeoutVa); push(completionIoStatusVa); push(completionApcVa);
    push(completionKeyVa); guest.PushMemory(duplicateHandleVa); call(16u); save(80u);
    push(completionInfoVa); guest.PushMemory(duplicateHandleVa); call(15u); save(84u);
    push(timeoutVa); push(completionIoStatusVa); push(completionApcVa);
    push(completionKeyVa); guest.PushMemory(duplicateHandleVa); call(16u); save(88u);
    guest.PushMemory(duplicateHandleVa); call(19u); save(92u);
    push(0x1234u); push(0xC0000001u); push(0x33334444u); push(0x11112222u);
    guest.PushMemory(duplicateHandleVa); call(17u); save(96u);
    guest.Emit8(0xC3); // ret

    XBE_IMAGE_INFO info{};
    info.baseAddress = guestBase;
    info.imageSize = imageSize;
    info.headerSize = headerSize;
    info.entryPoint = entryPoint;
    info.sectionCount = 1;
    info.tlsAddress = guestBase + tlsDirectoryOffset;
    info.kernelThunkAddress = guestBase + thunkTableOffset;
    info.nonKernelImportDirectoryAddress = 0;

    const int resolve = backend.resolveImports(image, &info, backend.context);
    if (resolve != 0)
        return Fail("guest import setup failed", resolve, &backend, image, stop);

    const int execute = backend.startGuest(&info, image, backend.context);
    if (execute != 0)
        return Fail("32-bit guest entry did not start", execute, &backend, image, stop);

    const auto readMarker = [&](std::uint32_t offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, image + offset, sizeof(value));
        return value;
    };
    DWORD waited = 0;
    while (waited < 5000 && readMarker(detachMarkerOffset) == 0) {
        Sleep(1);
        ++waited;
    }
    const std::uint32_t tlsIndex = readMarker(tlsIndexMarkerOffset);
    if (tlsIndex == TLS_OUT_OF_INDEXES ||
        readMarker(attachMarkerOffset) != 1u ||
        readMarker(tlsCheckMarkerOffset) != 1u ||
        readMarker(detachMarkerOffset) != 1u)
        return Fail("TLS template/zero-fill or attach-detach callback validation failed",
                    5, &backend, image, stop);

    if (readMarker(markerOffset + 0u) != 0u || readMarker(markerOffset + 4u) != 0u ||
        readMarker(markerOffset + 8u) != 1u || readMarker(markerOffset + 12u) != 0x102u ||
        readMarker(markerOffset + 16u) != 0u || readMarker(markerOffset + 20u) != 0u ||
        readMarker(markerOffset + 24u) != 0x102u || readMarker(markerOffset + 28u) != 0u ||
        readMarker(markerOffset + 32u) != 0u || readMarker(markerOffset + 36u) != 0u ||
        readMarker(markerOffset + 40u) != 1u || readMarker(markerOffset + 44u) != 0u ||
        readMarker(markerOffset + 48u) != 1u ||
        readMarker(markerOffset + 52u) != 1u ||
        readMarker(markerOffset + 56u) != 0u ||
        readMarker(markerOffset + 60u) != 0u ||
        readMarker(markerOffset + 64u) != 0u ||
        readMarker(markerOffset + 68u) != 0u ||
        readMarker(markerOffset + 72u) != 0u ||
        readMarker(markerOffset + 76u) != 0u ||
        readMarker(markerOffset + 80u) != 0u ||
        readMarker(markerOffset + 84u) != 0u ||
        readMarker(markerOffset + 88u) != 0x102u ||
        readMarker(markerOffset + 92u) != 0u ||
        readMarker(markerOffset + 96u) != 0xC0000008u ||
        *reinterpret_cast<const std::uint32_t*>(image + threadOutOffset) != 0u)
        return Fail("guest-call synchronization/I/O completion ABI assertions failed",
                    6, &backend, image, stop);

    const auto* completionInfo = reinterpret_cast<const GuestIoCompletionBasicInformation32*>(image + completionInfoOffset);
    const auto* completionIoStatus = reinterpret_cast<const GuestIoStatusBlock32*>(image + completionIoStatusOffset);
    const std::uint32_t completionHandle = *reinterpret_cast<const std::uint32_t*>(image + completionHandleOffset);
    const std::uint32_t duplicateHandle = *reinterpret_cast<const std::uint32_t*>(image + duplicateHandleOffset);
    if (!completionHandle || !duplicateHandle || completionHandle == duplicateHandle ||
        completionInfo->Depth != 0 ||
        *reinterpret_cast<const std::uint32_t*>(image + completionKeyOffset) != 0x11112222u ||
        *reinterpret_cast<const std::uint32_t*>(image + completionApcOffset) != 0x33334444u ||
        completionIoStatus->Status != LONG(0xC0000001u) ||
        completionIoStatus->Information != 0x1234u)
        return Fail("guest I/O completion packet/depth assertions failed",
                    8, &backend, image, stop);

    const auto* event = reinterpret_cast<const GuestEvent32*>(image + eventOffset);
    const auto* semaphore = reinterpret_cast<const GuestSemaphore32*>(image + semaphoreOffset);
    const auto* mutant = reinterpret_cast<const GuestMutant32*>(image + mutantOffset);
    const auto* timer = reinterpret_cast<const GuestTimer32*>(image + timerOffset);
    if (event->Header.Type != 0u || event->Header.Size != 4u ||
        event->Header.SignalState != 0 ||
        event->Header.WaitListHead.Flink != guestBase + eventOffset + 8u ||
        event->Header.WaitListHead.Blink != guestBase + eventOffset + 8u ||
        semaphore->Header.Type != 5u || semaphore->Header.Size != 5u ||
        semaphore->Header.SignalState != 0 || semaphore->Limit != 1 ||
        mutant->Header.Type != 2u || mutant->Header.Size != 8u ||
        mutant->Header.SignalState != 1 ||
        mutant->MutantListEntry.Flink != guestBase + mutantOffset + 16u ||
        mutant->MutantListEntry.Blink != guestBase + mutantOffset + 16u ||
        timer->Header.Type != 8u || timer->Header.Size != 10u ||
        timer->Header.Inserted != 0u || timer->Period != 5 ||
        timer->DueTime != static_cast<std::uint64_t>(relativeDueTime) ||
        timer->TimerListEntry.Flink != guestBase + timerOffset + 24u ||
        timer->TimerListEntry.Blink != guestBase + timerOffset + 24u)
        return Fail("guest dispatcher object layout/state assertions failed",
                    7, &backend, image, stop);

    backend.stopGuest(backend.context);
    backend.releaseImage(image, backend.context);
    stop();
    FreeLibrary(module);
    std::puts("kernel_guest_backend_smoke: PASS (shared mapping + x86 guest kernel dispatch calls + NXDK dispatcher layouts + TLS + stop/release)");
    return 0;
}
