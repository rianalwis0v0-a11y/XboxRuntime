
#pragma once

#ifndef XBOX_RUNTIME_BOOT_H
#define XBOX_RUNTIME_BOOT_H

#include <windows.h>

namespace XboxBoot
{
    // Executable and asset paths
    bool InitializePaths(HINSTANCE instance);

    const wchar_t* GetBaseDirectory();
    const wchar_t* GetAssetPath();
    const wchar_t* GetAnimationPath();
    const wchar_t* GetSoundPath();
    const wchar_t* GetMainDllPath();

    // Startup sequence
    bool InitializeHost(HINSTANCE instance);
    bool PlayStartupAnimation(HWND window);
    bool PlayStartupSound();
    bool StartRuntime();
    void StopRuntime();

    // Main window
    LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );
}

#endif
