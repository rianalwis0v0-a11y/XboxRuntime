#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "xbe_loader.h"
#include "xbe_format.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr std::uint64_t kMaximumXbeFileSize = 512ull * 1024ull * 1024ull;
std::mutex g_lock;
bool g_initialized = false;
bool g_imageActive = false;
std::vector<std::uint8_t> g_file;
std::uint8_t* g_guestImage = nullptr;
XBE_EXECUTION_BACKEND g_backend{};

void Log(const wchar_t* message) {
    OutputDebugStringW(L"[XboxRuntime:XBE] ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

void LogError(const char* prefix, XboxXbe::Error error) {
    char message[256]{};
    std::snprintf(message, sizeof(message), "%s: %s (detail=0x%08X)",
                  prefix, XboxXbe::ErrorName(error), 0u);
    wchar_t wide[256]{};
    MultiByteToWideChar(CP_UTF8, 0, message, -1, wide,
                        static_cast<int>(_countof(wide)));
    Log(wide);
}

void ReleaseActiveImage() {
    if (g_imageActive && g_backend.stopGuest)
        g_backend.stopGuest(g_backend.context);
    if (g_guestImage && g_backend.releaseImage)
        g_backend.releaseImage(g_guestImage, g_backend.context);
    g_guestImage = nullptr;
    g_imageActive = false;
    g_file.clear();
}

bool ReadWholeFile(const wchar_t* path, std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL |
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <
        static_cast<LONGLONG>(XboxXbe::kMinimumHeaderSize) ||
        static_cast<std::uint64_t>(size.QuadPart) > kMaximumXbeFileSize) {
        CloseHandle(file);
        return false;
    }

    try {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
    } catch (...) {
        CloseHandle(file);
        return false;
    }

    std::size_t total = 0;
    while (total < bytes.size()) {
        const DWORD request = static_cast<DWORD>(
            (std::min)(bytes.size() - total,
                       static_cast<std::size_t>(0x400000)));
        DWORD received = 0;
        if (!ReadFile(file, bytes.data() + total, request, &received, nullptr) ||
            received == 0) {
            CloseHandle(file);
            bytes.clear();
            return false;
        }
        total += received;
    }

    CloseHandle(file);
    return true;
}
} // namespace

extern "C" int __cdecl ModuleStart() {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_initialized) return 0;
    g_initialized = true;
    Log(L"XBE loader module initialized.");
    return 0;
}

extern "C" void __cdecl ModuleStop() {
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_initialized) return;
    ReleaseActiveImage();
    g_backend = {};
    g_initialized = false;
    Log(L"XBE loader module stopped.");
}

extern "C" int __cdecl XBE_RegisterExecutionBackend(
    const XBE_EXECUTION_BACKEND* backend) {
    if (!backend || backend->size < sizeof(XBE_EXECUTION_BACKEND) ||
        backend->version != XBE_EXECUTION_BACKEND_VERSION ||
        !backend->reserveImage || !backend->releaseImage ||
        !backend->resolveKernelImports || !backend->startGuest ||
        !backend->stopGuest)
        return 1;

    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_initialized || g_imageActive) return 2;
    g_backend = *backend;
    return 0;
}

extern "C" int __cdecl LoadXbe(const wchar_t* path) {
    if (!path || !*path) return 1;
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_initialized) return 2;
    if (g_imageActive) return 3;
    if (!g_backend.reserveImage || !g_backend.releaseImage ||
        !g_backend.resolveKernelImports || !g_backend.startGuest ||
        !g_backend.stopGuest) {
        Log(L"No guest execution backend is registered; refusing to fake XBE execution.");
        return 4;
    }

    std::vector<std::uint8_t> file;
    if (!ReadWholeFile(path, file)) {
        Log(L"Could not read XBE file or file exceeds safety limit.");
        return 5;
    }

    XboxXbe::Image parsed{};
    const XboxXbe::Result parsedResult =
        XboxXbe::Parse(file.data(), file.size(), &parsed);
    if (!parsedResult) {
        LogError("XBE validation failed", parsedResult.error);
        return 6;
    }

    XBE_IMAGE_INFO info{};
    info.baseAddress = parsed.baseAddress;
    info.imageSize = parsed.imageSize;
    info.headerSize = parsed.headerSize;
    info.entryPoint = parsed.entryPoint;
    info.tlsAddress = 0; // TLS is initialized by the guest backend in a later phase.
    info.kernelThunkAddress = parsed.kernelThunkAddress;
    info.sectionCount = parsed.sectionCount;
    info.debugImage = parsed.debugImage ? 1u : 0u;

    std::uint8_t* guestImage = nullptr;
    if (g_backend.reserveImage(info.baseAddress, info.imageSize,
                               &guestImage, g_backend.context) != 0 ||
        !guestImage) {
        Log(L"Guest address-space reservation failed.");
        return 7;
    }

    const XboxXbe::Result mapped =
        XboxXbe::MapImage(file.data(), file.size(), parsed,
                          guestImage, info.imageSize);
    if (!mapped) {
        LogError("XBE image mapping failed", mapped.error);
        g_backend.releaseImage(guestImage, g_backend.context);
        return 8;
    }

    if (g_backend.resolveKernelImports(guestImage, &info,
                                       g_backend.context) != 0) {
        Log(L"Kernel import resolution failed.");
        g_backend.releaseImage(guestImage, g_backend.context);
        return 9;
    }

    if (g_backend.startGuest(info.entryPoint, info.tlsAddress,
                             g_backend.context) != 0) {
        Log(L"Guest entry-point startup failed.");
        g_backend.releaseImage(guestImage, g_backend.context);
        return 10;
    }

    g_file = std::move(file);
    g_guestImage = guestImage;
    g_imageActive = true;
    Log(L"XBE mapped and guest execution backend accepted entry point.");
    return 0;
}

extern "C" void __cdecl UnloadXbe() {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_initialized) ReleaseActiveImage();
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
