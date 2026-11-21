#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cwchar>

namespace {
using DirectSoundCreateFn = HRESULT (WINAPI*)(LPCGUID, void**, void*);
using DirectSoundCreate8Fn = HRESULT (WINAPI*)(LPCGUID, void**, void*);

INIT_ONCE g_dsoundOnce = INIT_ONCE_STATIC_INIT;
HMODULE g_dsoundModule = nullptr;
DirectSoundCreateFn g_create = nullptr;
DirectSoundCreate8Fn g_create8 = nullptr;

BOOL CALLBACK LoadSystemDirectSound(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (!length || length >= MAX_PATH) return TRUE;

    wchar_t path[MAX_PATH]{};
    if (wcscpy_s(path, MAX_PATH, systemDirectory) != 0 ||
        wcscat_s(path, MAX_PATH, L"\\dsound.dll") != 0)
        return TRUE;

    HMODULE module = LoadLibraryW(path);
    if (!module) return TRUE;

    auto create = reinterpret_cast<DirectSoundCreateFn>(
        GetProcAddress(module, "DirectSoundCreate"));
    auto create8 = reinterpret_cast<DirectSoundCreate8Fn>(
        GetProcAddress(module, "DirectSoundCreate8"));
    if (!create && !create8) {
        FreeLibrary(module);
        return TRUE;
    }
    g_dsoundModule = module;
    g_create = create;
    g_create8 = create8;
    return TRUE;
}

bool EnsureDirectSound() {
    return InitOnceExecuteOnce(&g_dsoundOnce, LoadSystemDirectSound,
                               nullptr, nullptr) &&
           g_dsoundModule != nullptr;
}
}

extern "C" HRESULT WINAPI DirectSoundCreate(
    LPCGUID device, void** sound, void* outer) {
    if (!sound) return E_INVALIDARG;
    *sound = nullptr;
    if (outer) return static_cast<HRESULT>(0x8878000AL); /* DSERR_NOAGGREGATION */
    if (!EnsureDirectSound() || !g_create)
        return static_cast<HRESULT>(0x88780078L); /* DSERR_NODRIVER */
    return g_create(device, sound, outer);
}

extern "C" HRESULT WINAPI DirectSoundCreate8(
    LPCGUID device, void** sound, void* outer) {
    if (!sound) return E_INVALIDARG;
    *sound = nullptr;
    if (outer) return static_cast<HRESULT>(0x8878000AL); /* DSERR_NOAGGREGATION */
    if (!EnsureDirectSound() || !g_create8)
        return static_cast<HRESULT>(0x88780078L); /* DSERR_NODRIVER */
    return g_create8(device, sound, outer);
}
