
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>

#include <string>

#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

// Main DLL exports
using MainStartFn = int (__cdecl*)();
using MainStopFn  = void (__cdecl*)();

// Application state
static HMODULE g_mainModule = nullptr;
static MainStopFn g_mainStop = nullptr;

static HWND g_animationWindow = nullptr;

static bool g_runtimeStarted = false;
static bool g_startupFinished = false;

static constexpr wchar_t WINDOW_CLASS[] =
    L"XboxRuntimeBootWindow";

static constexpr UINT WM_START_RUNTIME =
    WM_APP + 1;


// --------------------------------------------------
// Initialize Windows host environment
// --------------------------------------------------

static bool InitializeHost()
{
    INITCOMMONCONTROLSEX controls = {};

    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_ANIMATE_CLASS;

    if (!InitCommonControlsEx(&controls))
        return false;

    return true;
}


// --------------------------------------------------
// Load main.dll from Windows System32
// --------------------------------------------------

static bool StartRuntime()
{
    if (g_runtimeStarted)
        return true;

    wchar_t systemDirectory[MAX_PATH] = {};

    UINT length = GetSystemDirectoryW(
        systemDirectory,
        MAX_PATH
    );

    if (length == 0 || length >= MAX_PATH)
        return false;

    std::wstring dllPath(systemDirectory);
    dllPath += L"\\main.dll";

    g_mainModule = LoadLibraryW(dllPath.c_str());

    if (!g_mainModule)
        return false;

    auto mainStart = reinterpret_cast<MainStartFn>(
        GetProcAddress(g_mainModule, "MainStart")
    );

    g_mainStop = reinterpret_cast<MainStopFn>(
        GetProcAddress(g_mainModule, "MainStop")
    );

    if (!mainStart)
    {
        FreeLibrary(g_mainModule);
        g_mainModule = nullptr;
        return false;
    }

    // MainStart initializes the runtime and starts
    // persistent module workers. It must return after
    // successful initialization, not block forever.

    if (!mainStart())
    {
        if (g_mainStop)
            g_mainStop();

        FreeLibrary(g_mainModule);

        g_mainModule = nullptr;
        g_mainStop = nullptr;

        return false;
    }

    g_runtimeStarted = true;
    return true;
}


// --------------------------------------------------
// Shut down the runtime
// --------------------------------------------------

static void StopRuntime()
{
    if (g_runtimeStarted && g_mainStop)
        g_mainStop();

    g_runtimeStarted = false;
    g_mainStop = nullptr;

    if (g_mainModule)
    {
        FreeLibrary(g_mainModule);
        g_mainModule = nullptr;
    }
}


// --------------------------------------------------
// Start runtime after animation
// --------------------------------------------------

static void FinishStartup(HWND hwnd)
{
    if (g_startupFinished)
        return;

    g_startupFinished = true;

    if (g_animationWindow)
    {
        Animate_Stop(g_animationWindow);
        Animate_Close(g_animationWindow);

        DestroyWindow(g_animationWindow);
        g_animationWindow = nullptr;
    }

    PlaySoundW(nullptr, nullptr, 0);

    if (!StartRuntime())
    {
        MessageBoxW(
            hwnd,
            L"XboxRuntime failed to start.\n\n"
            L"Check that main.dll exists in "
            L"Windows\\System32 and exports MainStart.",
            L"XboxRuntime Boot Error",
            MB_OK | MB_ICONERROR
        );

        DestroyWindow(hwnd);
        return;
    }

    // The runtime is now active.
    // boot.exe remains alive as its process host.
}


// --------------------------------------------------
// Main window procedure
// --------------------------------------------------

static LRESULT CALLBACK BootWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_animationWindow = CreateWindowExW(
            0,
            ANIMATE_CLASSW,
            nullptr,
            WS_CHILD | WS_VISIBLE | ACS_CENTER,
            0, 0, 0, 0,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!g_animationWindow)
            return -1;

        if (!Animate_Open(
            g_animationWindow,
            MAKEINTRESOURCEW(IDR_BOOT_ANIMATION)))
        {
            return -1;
        }

        // Play the embedded startup sound.
        PlaySoundW(
            MAKEINTRESOURCEW(IDR_BOOT_SOUND),
            GetModuleHandleW(nullptr),
            SND_RESOURCE | SND_ASYNC | SND_NODEFAULT
        );

        // Play the animation exactly once.
        if (!Animate_Play(
            g_animationWindow,
            0,
            -1,
            1))
        {
            return -1;
        }

        return 0;
    }

    case WM_SIZE:
    {
        if (g_animationWindow)
        {
            MoveWindow(
                g_animationWindow,
                0,
                0,
                LOWORD(lParam),
                HIWORD(lParam),
                TRUE
            );
        }

        return 0;
    }

    case WM_NOTIFY:
    {
        auto notification =
            reinterpret_cast<LPNMHDR>(lParam);

        if (notification &&
            notification->hwndFrom == g_animationWindow &&
            notification->code == ACN_STOP)
        {
            PostMessageW(
                hwnd,
                WM_START_RUNTIME,
                0,
                0
            );

            return 0;
        }

        break;
    }

    case WM_START_RUNTIME:
    {
        FinishStartup(hwnd);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        StopRuntime();
        PlaySoundW(nullptr, nullptr, 0);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}


// --------------------------------------------------
// Executable entry point
// --------------------------------------------------

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    if (!InitializeHost())
        return 1;

    WNDCLASSEXW windowClass = {};

    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = BootWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(
        nullptr,
        IDC_ARROW
    );

    windowClass.hbrBackground =
        static_cast<HBRUSH>(
            GetStockObject(BLACK_BRUSH)
        );

    windowClass.lpszClassName = WINDOW_CLASS;

    if (!RegisterClassExW(&windowClass))
        return 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        WINDOW_CLASS,
        L"XboxRuntime",
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd)
        return 3;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG message = {};

    while (true)
    {
        BOOL result = GetMessageW(
            &message,
            nullptr,
            0,
            0
        );

        if (result == 0)
            break;

        if (result == -1)
            return 4;

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
