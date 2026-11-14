#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../loader-src/modules/xbe_loader/xbe_backend_api.h"
#include "../loader-src/modules/xbe_loader/xbe_format.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using ModuleStart = int (__cdecl*)();
using ModuleStop = void (__cdecl*)();
using GetBackendV2 = int (__cdecl*)(XBE_EXECUTION_BACKEND_V2*);
using RegisterBackendV2 = int (__cdecl*)(const XBE_EXECUTION_BACKEND_V2*);
using LoadXbe = int (__cdecl*)(const wchar_t*);
using UnloadXbe = void (__cdecl*)();
using WaitForGuestExit = int (__cdecl*)(std::uint32_t, std::uint32_t*, std::uint32_t*, std::uint32_t*, std::uint32_t*, std::uint32_t*, std::uint32_t*, std::uint32_t*);
using GetReserveDiagnostic = int (__cdecl*)(int*, DWORD*, DWORD*);

static bool ReadFileBytes(const wchar_t* path, std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > 512ll * 1024ll * 1024ll) {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                             &read, nullptr);
    CloseHandle(file);
    return ok && read == bytes.size();
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fwprintf(stderr, L"usage: kernel_xbe_integration.exe <default.xbe>\n");
        return 2;
    }

    std::vector<std::uint8_t> file;
    if (!ReadFileBytes(argv[1], file)) {
        std::fwprintf(stderr, L"FAIL: cannot read NXDK XBE, error=%lu\n", GetLastError());
        return 3;
    }
    XboxXbe::Image image{};
    const XboxXbe::Result parsed = XboxXbe::Parse(file.data(), file.size(), &image);
    if (!parsed) {
        std::fprintf(stderr, "FAIL: parser rejected fixture: %s detail=0x%08X\n",
                     XboxXbe::ErrorName(parsed.error), parsed.detail);
        return 4;
    }
    std::printf("NXDK XBE metadata: base=0x%08X size=0x%08X entry=0x%08X "
                "TLS=0x%08X sections=%u\n",
                image.baseAddress, image.imageSize, image.entryPoint,
                image.tlsAddress, image.sectionCount);
    for (std::uint32_t i = 0; i < image.sectionCount; ++i) {
        const auto& section = image.sections[i];
        std::printf("Section %u: flags=0x%08X VA=0x%08X virtual=0x%08X raw=0x%08X rawSize=0x%08X\n",
                    i, section.flags, section.virtualAddress, section.virtualSize,
                    section.rawAddress, section.rawSize);
    }
    if (!image.tlsAddress || !image.sectionCount ||
        image.entryPoint < image.baseAddress ||
        static_cast<std::uint64_t>(image.entryPoint) >=
            static_cast<std::uint64_t>(image.baseAddress) + image.imageSize) {
        std::fprintf(stderr, "FAIL: fixture lacks required entry/TLS metadata\n");
        return 5;
    }

    HMODULE kernel = LoadLibraryW(L"xbox_kernel.dll");
    if (!kernel) {
        std::fprintf(stderr, "FAIL: LoadLibrary xbox_kernel.dll error=%lu\n", GetLastError());
        return 6;
    }
    HMODULE loader = LoadLibraryW(L"xbe_loader.dll");
    if (!loader) {
        std::fprintf(stderr, "FAIL: LoadLibrary xbe_loader.dll error=%lu\n", GetLastError());
        FreeLibrary(kernel);
        return 7;
    }

    auto kernelStart = reinterpret_cast<ModuleStart>(GetProcAddress(kernel, "ModuleStart"));
    auto kernelStop = reinterpret_cast<ModuleStop>(GetProcAddress(kernel, "ModuleStop"));
    auto getBackend = reinterpret_cast<GetBackendV2>(
        GetProcAddress(kernel, "XBE_GetExecutionBackendV2"));
    auto waitForExit = reinterpret_cast<WaitForGuestExit>(
        GetProcAddress(kernel, "XboxKernelWaitForGuestExit"));
    auto getReserveDiagnostic = reinterpret_cast<GetReserveDiagnostic>(
        GetProcAddress(kernel, "XboxKernelGetLastReserveDiagnostic"));
    auto loaderStart = reinterpret_cast<ModuleStart>(GetProcAddress(loader, "ModuleStart"));
    auto loaderStop = reinterpret_cast<ModuleStop>(GetProcAddress(loader, "ModuleStop"));
    auto registerBackend = reinterpret_cast<RegisterBackendV2>(
        GetProcAddress(loader, "XBE_RegisterExecutionBackendV2"));
    auto loadXbe = reinterpret_cast<LoadXbe>(GetProcAddress(loader, "LoadXbe"));
    auto unloadXbe = reinterpret_cast<UnloadXbe>(GetProcAddress(loader, "UnloadXbe"));
    if (!kernelStart || !kernelStop || !getBackend || !waitForExit ||
        !getReserveDiagnostic || !loaderStart || !loaderStop || !registerBackend || !loadXbe || !unloadXbe) {
        std::fprintf(stderr, "FAIL: required kernel/loader export missing\n");
        FreeLibrary(loader);
        FreeLibrary(kernel);
        return 8;
    }

    int result = 0;
    bool kernelStarted = false;
    bool loaderStarted = false;
    bool xbeActive = false;
    if (kernelStart() != 0) {
        std::fprintf(stderr, "FAIL: kernel ModuleStart\n");
        result = 9;
    } else {
        kernelStarted = true;
    }
    if (!result && loaderStart() != 0) {
        std::fprintf(stderr, "FAIL: loader ModuleStart\n");
        result = 10;
    } else if (!result) {
        loaderStarted = true;
    }

    XBE_EXECUTION_BACKEND_V2 backend{};
    backend.size = sizeof(backend);
    backend.version = XBE_EXECUTION_BACKEND_V2_VERSION;
    if (!result && (getBackend(&backend) != 0 ||
                    registerBackend(&backend) != 0)) {
        std::fprintf(stderr, "FAIL: ABI v2 provider registration\n");
        result = 11;
    }

    if (!result) {
        const int loadResult = loadXbe(argv[1]);
        if (loadResult != 0) {
            int reserveResult = -1;
            DWORD win32Error = 0;
            DWORD childExitCode = 0;
            getReserveDiagnostic(&reserveResult, &win32Error, &childExitCode);
            std::fprintf(stderr,
                "FAIL: LoadXbe returned %d; reserveResult=%d win32Error=0x%08lX childExit=0x%08lX\n",
                loadResult, reserveResult, win32Error, childExitCode);
            result = 12;
        } else {
            xbeActive = true;
            std::puts("PASS: LoadXbe mapped the on-disk NXDK image and the real kernel backend accepted startup.");
        }
    }

    if (!result) {
        struct GuestTlsDirectory {
            std::uint32_t start, end, index, callbacks, zeroFill, characteristics;
        } tls{};
        SIZE_T copied = 0;
        if (ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(image.tlsAddress)),
                &tls, sizeof(tls), &copied) && copied == sizeof(tls)) {
            std::printf("TLS directory: start=0x%08X end=0x%08X index=0x%08X callbacks=0x%08X zeroFill=0x%08X characteristics=0x%08X\\n",
                tls.start, tls.end, tls.index, tls.callbacks, tls.zeroFill, tls.characteristics);
            if (tls.callbacks) {
                for (std::uint32_t n = 0; n < 16; ++n) {
                    std::uint32_t callback = 0;
                    copied = 0;
                    const auto slot = static_cast<std::uintptr_t>(tls.callbacks) + n * sizeof(callback);
                    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(slot),
                            &callback, sizeof(callback), &copied) || copied != sizeof(callback)) {
                        std::printf("TLS callback[%u]: unreadable\\n", n);
                        break;
                    }
                    if (!callback) {
                        std::printf("TLS callback[%u]: <terminator>\\n", n);
                        break;
                    }
                    const XboxXbe::Section* owner = nullptr;
                    for (const auto& section : image.sections) {
                        const std::uint64_t end = std::uint64_t(section.virtualAddress) + section.virtualSize;
                        if (callback >= section.virtualAddress && callback < end) {
                            owner = &section;
                            break;
                        }
                    }
                    std::printf("TLS callback[%u]=0x%08X sectionFlags=%s",
                        n, callback, owner ? "" : "<outside image>\\n");
                    if (owner) std::printf("0x%08X\\n", owner->flags);
                    std::uint8_t bytes[16]{};
                    copied = 0;
                    if (ReadProcessMemory(GetCurrentProcess(),
                            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(callback)),
                            bytes, sizeof(bytes), &copied) && copied) {
                        std::printf("  callback bytes:");
                        for (SIZE_T b = 0; b < copied; ++b) std::printf(" %02X", bytes[b]);
                        std::printf("\\n");
                    }
                }
            }
        } else {
            std::printf("TLS directory diagnostic read failed: %lu\\n", GetLastError());
        }
    }

    if (!result) {
        std::uint32_t exitCode = 0xFFFFFFFFu;
        std::uint32_t tlsCleared = 0;
        std::uint32_t exceptionInstruction = 0;
        std::uint32_t accessType = 0xFFFFFFFFu;
        std::uint32_t faultAddress = 0;
        std::uint32_t faultProtection = 0;
        std::uint32_t faultState = 0;
        const int waitResult = waitForExit(30000, &exitCode, &tlsCleared,
            &exceptionInstruction, &accessType, &faultAddress,
            &faultProtection, &faultState);
        if (exceptionInstruction) {
            std::uint8_t faultBytes[32]{};
            SIZE_T faultCopied = 0;
            if (ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(exceptionInstruction)),
                    faultBytes, sizeof(faultBytes), &faultCopied) && faultCopied) {
                std::printf("Fault instruction bytes at 0x%08X:", exceptionInstruction);
                for (SIZE_T i = 0; i < faultCopied; ++i)
                    std::printf(" %02X", faultBytes[i]);
                std::printf("\\n");
            } else {
                std::printf("Fault instruction bytes unavailable: %lu\\n", GetLastError());
            }
            MEMORY_BASIC_INFORMATION faultMemory{};
            if (VirtualQuery(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(exceptionInstruction)),
                    &faultMemory, sizeof(faultMemory)) == sizeof(faultMemory)) {
                std::printf("Fault page: base=%p size=0x%Ix state=0x%lX protect=0x%lX type=0x%lX\\n",
                    faultMemory.BaseAddress, faultMemory.RegionSize, faultMemory.State,
                    faultMemory.Protect, faultMemory.Type);
            }
        }

        std::printf("Guest exit: wait=%d exitCode=0x%08X tlsIndexCleared=%u "
                    "exceptionIP=0x%08X accessType=%u faultAddress=0x%08X "
                    "faultProtect=0x%08X faultState=0x%08X\n",
                    waitResult, exitCode, tlsCleared, exceptionInstruction,
                    accessType, faultAddress, faultProtection, faultState);
        if (waitResult != ERROR_SUCCESS || exitCode != ERROR_SUCCESS ||
            tlsCleared != 1) {
            std::fprintf(stderr, "FAIL: guest did not return cleanly or TLS cleanup failed "
                "(exceptionIP=0x%08X accessType=%u faultAddress=0x%08X "
                "faultProtect=0x%08X faultState=0x%08X)\n",
                exceptionInstruction, accessType, faultAddress,
                faultProtection, faultState);
            result = 13;
        } else {
            std::puts("PASS: real guest entrypoint returned and TLS index slot was cleared.");
        }
    }

    if (xbeActive) {
        unloadXbe();
        xbeActive = false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(image.baseAddress)),
            &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_FREE) {
        std::fprintf(stderr, "FAIL: guest host-view mapping was not released (state=0x%lX)\n",
                     memory.State);
        if (!result) result = 14;
    } else {
        std::puts("PASS: guest image VA was released after guest shutdown.");
    }

    if (loaderStarted) loaderStop();
    if (kernelStarted) kernelStop();
    FreeLibrary(loader);
    FreeLibrary(kernel);
    if (!result) std::puts("PASS: real NXDK XBE + ABI v2 kernel integration lifecycle.");
    return result;
}
