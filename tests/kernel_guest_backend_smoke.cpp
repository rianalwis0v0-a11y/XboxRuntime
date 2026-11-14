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
    constexpr std::uint32_t imageSize = 0x5000u;
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
        0x05u, entryPoint, 0x3000u, 0u, 0u, 0u, 0u, 0u, 0u, {}
    };
    std::memcpy(image + 0x400, &section, sizeof(section));

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
    const std::uint32_t callbackVa = guestBase + 0x2100u;
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
    std::memcpy(image + 0x2100, callback, used);

    // Build an actual guest entrypoint that calls four Xbox kernel imports
    // through the resolved x86 thunk table: PsQueryStatistics, IoCreateSymbolicLink,
    // IoDeleteSymbolicLink, and HalRegisterShutdownNotification.
    constexpr std::uint32_t apiMarkerOffset = 0x3310u;
    constexpr std::uint32_t shutdownMarkerOffset = 0x3314u;
    constexpr std::uint32_t statsOffset = 0x3400u;
    constexpr std::uint32_t linkNameOffset = 0x3420u;
    constexpr std::uint32_t deviceNameOffset = 0x3430u;
    constexpr std::uint32_t linkNameStringOffset = 0x3450u;
    constexpr std::uint32_t deviceNameStringOffset = 0x3460u;
    constexpr std::uint32_t shutdownRegistrationOffset = 0x3470u;
    constexpr std::uint32_t kernelThunkOffset = 0x3500u;

    std::uint32_t stats[3] = {12u, 0u, 0u};
    std::memcpy(image + statsOffset, stats, sizeof(stats));
    const char linkName[] = "\\??\\Z:";
    const char deviceName[] = "\\Device\\Harddisk0\\Partition1";
    std::memcpy(image + linkNameOffset, linkName, sizeof(linkName));
    std::memcpy(image + deviceNameOffset, deviceName, sizeof(deviceName));
    struct GuestObjectString {
        std::uint16_t length;
        std::uint16_t maximumLength;
        std::uint32_t buffer;
    };
    static_assert(sizeof(GuestObjectString) == 8, "x86 OBJECT_STRING layout");
    const GuestObjectString linkNameString{
        static_cast<std::uint16_t>(sizeof(linkName) - 1),
        static_cast<std::uint16_t>(sizeof(linkName) - 1),
        guestBase + linkNameOffset
    };
    const GuestObjectString deviceNameString{
        static_cast<std::uint16_t>(sizeof(deviceName) - 1),
        static_cast<std::uint16_t>(sizeof(deviceName) - 1),
        guestBase + deviceNameOffset
    };
    std::memcpy(image + linkNameStringOffset, &linkNameString, sizeof(linkNameString));
    std::memcpy(image + deviceNameStringOffset, &deviceNameString, sizeof(deviceNameString));

    const std::uint32_t shutdownRegistration[4] = {
        guestBase + 0x2300u, 7u, 0u, 0u
    };
    std::memcpy(image + shutdownRegistrationOffset, shutdownRegistration,
                sizeof(shutdownRegistration));

    const std::uint32_t thunkEntries[5] = {
        0x80000000u | 256u, 0x80000000u | 67u,
        0x80000000u | 69u, 0x80000000u | 47u, 0u
    };
    std::memcpy(image + kernelThunkOffset, thunkEntries, sizeof(thunkEntries));

    std::vector<std::uint8_t> shutdownCallback;
    auto callbackEmit8 = [&](std::uint8_t value) { shutdownCallback.push_back(value); };
    auto callbackEmit32 = [&](std::uint32_t value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        shutdownCallback.insert(shutdownCallback.end(), bytes, bytes + sizeof(value));
    };
    callbackEmit8(0xC7); callbackEmit8(0x05);
    callbackEmit32(guestBase + shutdownMarkerOffset);
    callbackEmit32(1u);
    callbackEmit8(0xC2); callbackEmit8(0x04); callbackEmit8(0x00); // ret 4
    std::memcpy(image + 0x2300, shutdownCallback.data(), shutdownCallback.size());

    std::vector<std::uint8_t> guestEntry;
    auto emit8 = [&](std::uint8_t value) { guestEntry.push_back(value); };
    auto emit32 = [&](std::uint32_t value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        guestEntry.insert(guestEntry.end(), bytes, bytes + sizeof(value));
    };
    auto push32 = [&](std::uint32_t value) { emit8(0x68); emit32(value); };
    auto callThunk = [&](std::uint32_t slotAddress) {
        emit8(0xFF); emit8(0x15); emit32(slotAddress); // call dword ptr [absolute]
    };
    auto testEax = [&]() { emit8(0x85); emit8(0xC0); };
    auto compareMemory = [&](std::uint32_t address, std::uint32_t value) {
        emit8(0x81); emit8(0x3D); emit32(address); emit32(value);
    };
    auto compareEax = [&](std::uint32_t value) { emit8(0x3D); emit32(value); };
    auto storeMemory = [&](std::uint32_t address, std::uint32_t value) {
        emit8(0xC7); emit8(0x05); emit32(address); emit32(value);
    };
    std::vector<std::size_t> failBranches;
    auto branchNotEqualToFailure = [&]() {
        emit8(0x0F); emit8(0x85); failBranches.push_back(guestEntry.size()); emit32(0);
    };
    auto branchLessEqualToFailure = [&]() {
        emit8(0x0F); emit8(0x8E); failBranches.push_back(guestEntry.size()); emit32(0);
    };

    const std::uint32_t thunkTableVa = guestBase + kernelThunkOffset;
    push32(guestBase + statsOffset);
    callThunk(thunkTableVa);
    testEax(); branchNotEqualToFailure();
    compareMemory(guestBase + statsOffset, 12u);
    branchNotEqualToFailure();
    compareMemory(guestBase + statsOffset + 4u, 0u);
    branchLessEqualToFailure();
    compareMemory(guestBase + statsOffset + 8u, 0u);
    branchLessEqualToFailure();

    push32(guestBase + deviceNameStringOffset);
    push32(guestBase + linkNameStringOffset);
    callThunk(thunkTableVa + 4u);
    testEax(); branchNotEqualToFailure();

    // A duplicate name must report STATUS_OBJECT_NAME_COLLISION.
    push32(guestBase + deviceNameStringOffset);
    push32(guestBase + linkNameStringOffset);
    callThunk(thunkTableVa + 4u);
    compareEax(0xC0000035u);
    branchNotEqualToFailure();

    push32(guestBase + linkNameStringOffset);
    callThunk(thunkTableVa + 8u);
    testEax(); branchNotEqualToFailure();

    // A second delete must report STATUS_OBJECT_NAME_NOT_FOUND.
    push32(guestBase + linkNameStringOffset);
    callThunk(thunkTableVa + 8u);
    compareEax(0xC0000034u);
    branchNotEqualToFailure();

    push32(1u);
    push32(guestBase + shutdownRegistrationOffset);
    callThunk(thunkTableVa + 12u);

    storeMemory(guestBase + apiMarkerOffset, 1u);
    emit8(0xC3);
    const std::size_t failureOffset = guestEntry.size();
    storeMemory(guestBase + apiMarkerOffset, 0xBADu);
    emit8(0xC3);
    for (const std::size_t displacement : failBranches) {
        const std::int32_t delta = static_cast<std::int32_t>(failureOffset) -
                                   static_cast<std::int32_t>(displacement + sizeof(delta));
        std::memcpy(guestEntry.data() + displacement, &delta, sizeof(delta));
    }
    std::memcpy(image + 0x2000, guestEntry.data(), guestEntry.size());

    XBE_IMAGE_INFO info{};
    info.baseAddress = guestBase;
    info.imageSize = imageSize;
    info.headerSize = headerSize;
    info.entryPoint = entryPoint;
    info.sectionCount = 1;
    info.tlsAddress = guestBase + tlsDirectoryOffset;
    info.kernelThunkAddress = guestBase + kernelThunkOffset;
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

    if (readMarker(apiMarkerOffset) != 1u)
        return Fail("real guest kernel import thunks did not complete all API calls",
                    7, &backend, image, stop);
    backend.stopGuest(backend.context);
    if (readMarker(shutdownMarkerOffset) != 1u) {
        std::fprintf(stderr, "kernel_guest_backend_smoke: shutdown notification callback was not invoked\\n");
        backend.releaseImage(image, backend.context);
        stop();
        FreeLibrary(module);
        return 8;
    }
    backend.releaseImage(image, backend.context);
    stop();
    FreeLibrary(module);
    std::puts("kernel_guest_backend_smoke: PASS (shared mapping + real kernel import thunks + PsQueryStatistics + Io* symbolic links + HAL shutdown callback + TLS + stop/release)");
    return 0;
}
