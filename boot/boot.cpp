
#include "boot.h"

#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

namespace XboxBoot
{
    static HINSTANCE g_instance = nullptr;
    static HWND g_animation = nullptr;
    static HMODULE g_mainModule = nullptr;

    using MainStartFunction = int (__cdecl*)();
    using MainStopFunction = void (__cdecl*)();

    static MainStartFunction g_mainStart = nullptr;
    static MainStopFunction g_mainStop = nullptr;

    static std::wstring g_systemDirectory;
    static std::wstring g_runtimeDirectory;
    static std::wstring g_animationPath;
    static std::wstring g_soundPath;

    static bool g_runtimeStarted = false;
    static bool g_startupFinished = false;

    static constexpr UINT WM_START_RUNTIME = WM_APP + 1;

    static std::wstring JoinPath(
        const std::wstring& first,
        const wchar_t* second)
    {
        return first + L"\\" + second;
    }

    static bool GetSystemDirectoryPath()
    {
        std::vector<wchar_t> buffer(32768);

        UINT length = GetSystemDirectoryW(
            buffer.data(),
            static_cast<UINT>(buffer.size())
        );

        if (length == 0 || length >= buffer.size())
            return false;

        g_systemDirectory.assign(buffer.data(), length);
        return true;
    }

    static bool GetRuntimeDirectoryPath()
    {
        // XboxRuntime data directory is expected beside
        // the Windows folder, e.g. X:\XboxRuntime.
        std::vector<wchar_t> buffer(32768);

        UINT length = GetWindowsDirectoryW(
            buffer.data(),
            static_cast<UINT>(buffer.size())
        );

        if (length == 0 || length >= buffer.size())
            return false;

        std::wstring windowsDirectory(buffer.data(), length);

        size_t separator =
            windowsDirectory.find_last_of(L"\\/");

        if (separator == std::wstring::npos)
            return false;

        std::wstring driveRoot =
            windowsDirectory.substr(0, separator);

        g_runtimeDirectory =
            JoinPath(driveRoot, L"XboxRuntime");

        return true;
    }

    bool InitializePaths(HINSTANCE)
    {
        if (!GetSystemDirectoryPath())
            return false;

        if (!GetRuntimeDirectoryPath())
            return false;

        g_animationPath = JoinPath(
            JoinPath(g_runtimeDirectory, L"assets"),
            L"startup.avi"
        );

        g_soundPath = JoinPath(
            JoinPath(g_runtimeDirectory, L"assets"),
            L"startup.wav"
        );

        return true;
    }

    const wchar_t* GetBaseDirectory()
    {
        return g_systemDirectory.c_str();
    }

    const wchar_t* GetAssetPath()
    {
        return g_runtimeDirectory.c_str();
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
        static std::wstring path;
        path = JoinPath(JoinPath(g_runtimeDirectory, L"system"), L"main.dll");
        return path.c_str();
    }

    bool InitializeHost(HINSTANCE instance)
    {
        g_instance = instance;

        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_ANIMATE_CLASS;

        if (!InitCommonControlsEx(&controls))
            return false;

        return InitializePaths(instance);
    }

    bool PlayStartupSound()
    {
        if (GetFileAttributesW(g_soundPath.c_str()) ==
            INVALID_FILE_ATTRIBUTES)
        {
            OutputDebugStringW(
                L"[XboxBoot] startup.wav not found.\n"
            );
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
            0, 0,
            client.right,
            client.bottom,
            SWP_SHOWWINDOW
        );

        return Animate_Play(
            g_animation,
            0,
            -1,
            1
        ) != FALSE;
    }

    bool StartRuntime()
    {
        if (g_runtimeStarted)
            return true;

        std::wstring mainPath =
            JoinPath(JoinPath(g_runtimeDirectory, L"system"), L"main.dll");

        g_mainModule = LoadLibraryExW(
            mainPath.c_str(),
            nullptr,
            LOAD_WITH_ALTERED_SEARCH_PATH
        );

        if (!g_mainModule)
        {
            OutputDebugStringW(
                L"[XboxBoot] Failed to load main.dll.\n"
            );
            return false;
        }

        g_mainStart = reinterpret_cast<MainStartFunction>(
            GetProcAddress(g_mainModule, "MainStart")
        );

        g_mainStop = reinterpret_cast<MainStopFunction>(
            GetProcAddress(g_mainModule, "MainStop")
        );

        if (!g_mainStart)
        {
            OutputDebugStringW(
                L"[XboxBoot] MainStart export missing.\n"
            );

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
            // The startup AVI is optional; missing media must not block boot.
            if (!PlayStartupAnimation(window))
            {
                OutputDebugStringW(
                    L"[XboxBoot] startup.avi unavailable; skipping animation.\\n"
                );

                if (g_animation)
                {
                    DestroyWindow(g_animation);
                    g_animation = nullptr;
                }

                PostMessageW(window, WM_START_RUNTIME, 0, 0);
                return 0;
            }

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
                    L"Could not start main.dll from XboxRuntime\\system.",
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

    const wchar_t CLASS_NAME[] =
        L"XboxRuntimeBootWindow";

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
