
#include "boot.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

namespace XboxBoot
{
    static HINSTANCE g_instance = nullptr;
    static HWND g_window = nullptr;
    static HWND g_animation = nullptr;
    static HMODULE g_mainModule = nullptr;

    using MainStartFunction = int (__cdecl*)();
    using MainStopFunction = void (__cdecl*)();

    static MainStartFunction g_mainStart = nullptr;
    static MainStopFunction g_mainStop = nullptr;

    static std::wstring g_baseDirectory;
    static std::wstring g_assetDirectory;
    static std::wstring g_animationPath;
    static std::wstring g_soundPath;
    static std::wstring g_mainDllPath;

    static bool g_runtimeStarted = false;
    static bool g_startupFinished = false;

    static constexpr UINT WM_START_RUNTIME = WM_APP + 1;

    static std::wstring JoinPath(
        const std::wstring& first,
        const wchar_t* second)
    {
        return first + L"\\" + second;
    }

    bool InitializePaths(HINSTANCE instance)
    {
        std::vector<wchar_t> buffer(32768);

        DWORD length = GetModuleFileNameW(
            instance,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        if (length == 0 || length >= buffer.size())
            return false;

        std::wstring executable(buffer.data(), length);

        size_t separator = executable.find_last_of(L"\\/");

        if (separator == std::wstring::npos)
            return false;

        g_baseDirectory = executable.substr(0, separator);

        g_assetDirectory = JoinPath(
            g_baseDirectory,
            L"assets"
        );

        g_animationPath = JoinPath(
            g_assetDirectory,
            L"startup.avi"
        );

        g_soundPath = JoinPath(
            g_assetDirectory,
            L"startup.wav"
        );

        g_mainDllPath = JoinPath(
            JoinPath(g_baseDirectory, L"System32"),
            L"main.dll"
        );

        return true;
    }

    const wchar_t* GetBaseDirectory()
    {
        return g_baseDirectory.c_str();
    }

    const wchar_t* GetAssetPath()
    {
        return g_assetDirectory.c_str();
    }

    const wchar_t* GetAnimationPath()
    {
        return g_animationPath.c_str();
    }

    const wchar_t* GetSoundPath()
    {
        return g_soundPath.c_str();
    }

    const wchar_t* GetMainDllPath()
    {
        return g_mainDllPath.c_str();
    }

    bool InitializeHost(HINSTANCE instance)
    {
        g_instance = instance;

        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_ANIMATE_CLASS;

        return InitializePaths(instance) &&
               InitCommonControlsEx(&controls);
    }

    bool PlayStartupSound()
    {
        if (GetFileAttributesW(g_soundPath.c_str()) ==
            INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }

        return PlaySoundW(
            g_soundPath.c_str(),
            nullptr,
            SND_FILENAME | SND_ASYNC | SND_NODEFAULT
        ) != FALSE;
    }

    bool PlayStartupAnimation(HWND window)
    {
        g_animation = CreateWindowExW(
            0,
            ANIMATE_CLASSW,
            nullptr,
            WS_CHILD | WS_VISIBLE | ACS_CENTER,
            0, 0, 0, 0,
            window,
            nullptr,
            g_instance,
            nullptr
        );

        if (!g_animation)
            return false;

        if (!Animate_Open(g_animation, g_animationPath.c_str()))
            return false;

        RECT client{};
        GetClientRect(window, &client);

        SetWindowPos(
            g_animation,
            HWND_TOP,
            0,
            0,
            client.right,
            client.bottom,
            SWP_SHOWWINDOW
        );

        return Animate_Play(g_animation, 0, -1, 1) != FALSE;
    }

    bool StartRuntime()
    {
        if (g_runtimeStarted)
            return true;

        g_mainModule = LoadLibraryW(g_mainDllPath.c_str());

        if (!g_mainModule)
            return false;

        g_mainStart = reinterpret_cast<MainStartFunction>(
            GetProcAddress(g_mainModule, "MainStart")
        );

        g_mainStop = reinterpret_cast<MainStopFunction>(
            GetProcAddress(g_mainModule, "MainStop")
        );

        if (!g_mainStart)
        {
            FreeLibrary(g_mainModule);
            g_mainModule = nullptr;
            return false;
        }

        if (g_mainStart() != 0)
        {
            if (g_mainStop)
                g_mainStop();

            FreeLibrary(g_mainModule);
            g_mainModule = nullptr;
            return false;
        }

        g_runtimeStarted = true;
        return true;
    }

    void StopRuntime()
    {
        if (g_animation)
        {
            Animate_Stop(g_animation);
            Animate_Close(g_animation);
            DestroyWindow(g_animation);
            g_animation = nullptr;
        }

        PlaySoundW(nullptr, nullptr, 0);

        if (g_runtimeStarted && g_mainStop)
            g_mainStop();

        g_runtimeStarted = false;

        if (g_mainModule)
        {
            FreeLibrary(g_mainModule);
            g_mainModule = nullptr;
        }

        g_mainStart = nullptr;
        g_mainStop = nullptr;
    }

    LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            g_window = window;

            if (!PlayStartupAnimation(window))
                return -1;

            PlayStartupSound();
            return 0;

        case WM_COMMAND:
            if (HIWORD(wParam) == ACN_STOP &&
                reinterpret_cast<HWND>(lParam) == g_animation)
            {
                if (!g_startupFinished)
                {
                    g_startupFinished = true;
                    PostMessageW(
                        window,
                        WM_START_RUNTIME,
                        0,
                        0
                    );
                }
            }
            return 0;

        case WM_START_RUNTIME:
            if (!StartRuntime())
            {
                MessageBoxW(
                    window,
                    L"Could not start System32\\main.dll.",
                    L"XboxRuntime",
                    MB_OK | MB_ICONERROR
                );

                DestroyWindow(window);
            }
            return 0;

        case WM_SIZE:
            if (g_animation)
            {
                SetWindowPos(
                    g_animation,
                    nullptr,
                    0, 0,
                    LOWORD(lParam),
                    HIWORD(lParam),
                    SWP_NOZORDER
                );
            }
            return 0;

        case WM_DESTROY:
            StopRuntime();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(
            window,
            message,
            wParam,
            lParam
        );
    }
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    using namespace XboxBoot;

    if (!InitializeHost(instance))
        return 1;

    const wchar_t CLASS_NAME[] = L"XboxRuntimeBootWindow";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = CLASS_NAME;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground =
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassW(&windowClass))
        return 2;

    HWND window = CreateWindowExW(
        WS_EX_TOPMOST,
        CLASS_NAME,
        L"XboxRuntime",
        WS_POPUP,
        0, 0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!window)
        return 3;

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
