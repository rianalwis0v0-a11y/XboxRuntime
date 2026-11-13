
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "main.h"
#include "../modules/xbe_loader/xbe_backend_api.h"

#include <cwchar>
#include <string>
#include <vector>

namespace XboxRuntime
{
    using ModuleStart = int (__cdecl*)();
    using ModuleStop = void (__cdecl*)();
    using LoadXbe = int (__cdecl*)(const wchar_t*);

    struct Module
    {
        const wchar_t* name;
        HMODULE handle = nullptr;
        ModuleStart start = nullptr;
        ModuleStop stop = nullptr;
        bool started = false;
    };

    static std::vector<Module> modules =
    {
        { L"eeprom.dll" },
        { L"xbox_storage.dll" },
        { L"fatx.dll" },
        { L"xdvdfs.dll" },
        { L"xbox_kernel.dll" },
        { L"nv2a.dll" },
        { L"xbox_graphics.dll" },
        { L"xbox_audio.dll" },
        { L"xbox_input.dll" },
        { L"xbox_network.dll" },
        { L"xbe_loader.dll" }
    };

    static std::wstring systemDirectory;
    static std::wstring runtimeDirectory;

    static bool running = false;

    static void Log(const wchar_t* message)
    {
        OutputDebugStringW(L"[XboxRuntime] ");
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
    }

    static bool FindDirectories()
    {
        wchar_t systemPath[32768]{};

        UINT length = GetSystemDirectoryW(
            systemPath,
            static_cast<UINT>(_countof(systemPath))
        );

        if (!length || length >= _countof(systemPath))
            return false;

        systemDirectory.assign(systemPath, length);

        wchar_t windowsPath[32768]{};

        length = GetWindowsDirectoryW(
            windowsPath,
            static_cast<UINT>(_countof(windowsPath))
        );

        if (!length || length >= _countof(windowsPath))
            return false;

        std::wstring windowsDirectory(windowsPath, length);

        size_t separator =
            windowsDirectory.find_last_of(L"\\/");

        if (separator == std::wstring::npos)
            return false;

        runtimeDirectory =
            windowsDirectory.substr(0, separator) +
            L"\\XboxRuntime";

        return true;
    }

    static std::wstring ModulePath(const wchar_t* name)
    {
        return systemDirectory + L"\\" + name;
    }

    static bool LoadModule(Module& module)
    {
        Log(module.name);

        std::wstring path = ModulePath(module.name);

        module.handle = LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_WITH_ALTERED_SEARCH_PATH
        );

        if (!module.handle)
        {
            Log(L"Module load failed.");
            return false;
        }

        module.start = reinterpret_cast<ModuleStart>(
            GetProcAddress(module.handle, "ModuleStart")
        );

        module.stop = reinterpret_cast<ModuleStop>(
            GetProcAddress(module.handle, "ModuleStop")
        );

        if (!module.start || !module.stop)
        {
            Log(L"Required module exports are missing.");
            FreeLibrary(module.handle);
            module.handle = nullptr;
            return false;
        }

        return true;
    }

    static bool RegisterXbeExecutionBackend()
    {
        HMODULE kernelHandle = nullptr;
        HMODULE loaderHandle = nullptr;

        for (const auto& module : modules)
        {
            if (wcscmp(module.name, L"xbox_kernel.dll") == 0)
                kernelHandle = module.handle;
            else if (wcscmp(module.name, L"xbe_loader.dll") == 0)
                loaderHandle = module.handle;
        }

        if (!kernelHandle || !loaderHandle)
        {
            Log(L"Kernel or XBE loader module is unavailable.");
            return false;
        }

        using GetBackend = int (__cdecl*)(XBE_EXECUTION_BACKEND*);
        using RegisterBackend = int (__cdecl*)(const XBE_EXECUTION_BACKEND*);

        auto getBackend = reinterpret_cast<GetBackend>(
            GetProcAddress(kernelHandle, "XBE_GetExecutionBackend")
        );
        auto registerBackend = reinterpret_cast<RegisterBackend>(
            GetProcAddress(loaderHandle, "XBE_RegisterExecutionBackend")
        );

        if (!getBackend || !registerBackend)
        {
            Log(L"Kernel XBE backend provider or loader registration export is missing.");
            return false;
        }

        XBE_EXECUTION_BACKEND backend{};
        backend.size = sizeof(backend);
        backend.version = XBE_EXECUTION_BACKEND_VERSION;

        if (getBackend(&backend) != 0 ||
            registerBackend(&backend) != 0)
        {
            Log(L"Could not connect the kernel guest execution backend.");
            return false;
        }

        Log(L"Kernel guest execution backend registered.");
        return true;
    }

    static void StopModules()
    {
        for (auto it = modules.rbegin();
             it != modules.rend();
             ++it)
        {
            if (it->started && it->stop)
            {
                Log(it->name);
                it->stop();
                it->started = false;
            }
        }

        for (auto it = modules.rbegin();
             it != modules.rend();
             ++it)
        {
            if (it->handle)
            {
                FreeLibrary(it->handle);
                it->handle = nullptr;
                it->start = nullptr;
                it->stop = nullptr;
            }
        }

        running = false;
    }

    static bool StartModules()
    {
        for (auto& module : modules)
        {
            if (!LoadModule(module))
            {
                StopModules();
                return false;
            }

            if (module.start() != 0)
            {
                Log(L"Module initialization failed.");

                // A failing module must clean up its own
                // partially initialized state.
                StopModules();
                return false;
            }

            module.started = true;
        }

        return true;
    }

    static bool StartDashboard()
    {
        Module& loader = modules.back();

        auto loadXbe = reinterpret_cast<LoadXbe>(
            GetProcAddress(loader.handle, "LoadXbe")
        );

        if (!loadXbe)
        {
            Log(L"LoadXbe export not found.");
            return false;
        }

        std::wstring dashboard =
            runtimeDirectory + L"\\dashboard.xbe";

        Log(L"Loading dashboard.xbe");

        return loadXbe(dashboard.c_str()) == 0;
    }
}

extern "C" int __cdecl MainStart()
{
    using namespace XboxRuntime;

    if (running)
        return 0;

    Log(L"Initializing XboxRuntime.");

    if (!FindDirectories())
    {
        Log(L"Could not locate system directories.");
        return 1;
    }

    if (!StartModules())
        return 2;

    if (!RegisterXbeExecutionBackend())
    {
        StopModules();
        return 3;
    }

    if (!StartDashboard())
    {
        StopModules();
        return 4;
    }

    running = true;

    Log(L"XboxRuntime initialized.");

    return 0;
}

extern "C" void __cdecl MainStop()
{
    using namespace XboxRuntime;

    if (!running)
        return;

    Log(L"Shutting down XboxRuntime.");

    StopModules();

    Log(L"XboxRuntime stopped.");
}

BOOL WINAPI DllMain(
    HINSTANCE,
    DWORD,
    LPVOID)
{
    return TRUE;
}
