
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "main.h"

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

    // Only implemented modules are started. Add each remaining Xbox
    // subsystem when its real implementation and build target are ready.
    static std::vector<Module> modules =
    {
        { L"nv2a.dll" },
        { L"insignia_dns.dll" },
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

    static bool FindDashboard(std::wstring& dashboardPath)
    {
        // GetLogicalDrives returns a bit for every currently assigned drive
        // letter (fixed, removable, optical, RAM disk, and mapped drives).
        const DWORD drives = GetLogicalDrives();
        if (!drives)
        {
            Log(L"Could not enumerate logical drives.");
            return false;
        }

        for (unsigned int index = 0; index < 26; ++index)
        {
            if ((drives & (1u << index)) == 0)
                continue;

            wchar_t root[] = L"A:\\\\";
            root[0] = static_cast<wchar_t>(L'A' + index);

            if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR)
                continue;

            const std::wstring driveRoot(root);

            // Prefer a dashboard placed directly in the drive root, then
            // support the conventional XboxRuntime subdirectory.
            const std::wstring candidates[] =
            {
                driveRoot + L"dashboard.xbe",
                driveRoot + L"XboxRuntime\\\\dashboard.xbe"
            };

            for (const auto& candidate : candidates)
            {
                const DWORD attributes = GetFileAttributesW(candidate.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    dashboardPath = candidate;
                    Log(L"Found dashboard.xbe on a mounted drive.");
                    return true;
                }
            }
        }

        Log(L"dashboard.xbe was not found in any mounted drive root.");
        return false;
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

        std::wstring dashboard;
        if (!FindDashboard(dashboard))
            return false;

        Log(L"Staging discovered dashboard.xbe in the native x86 guest process.");
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

    if (!StartDashboard())
    {
        StopModules();
        return 3;
    }

    running = true;

    Log(L"XboxRuntime host initialized; guest entry execution awaits the Xbox kernel.");

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
