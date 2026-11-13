#pragma once
#ifndef XBOX_RUNTIME_XBE_LOADER_H
#define XBOX_RUNTIME_XBE_LOADER_H

#include <windows.h>
#include <cstdint>
#include "xbe_backend_api.h"

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) int __cdecl ModuleStart();
__declspec(dllexport) void __cdecl ModuleStop();
__declspec(dllexport) int __cdecl XBE_RegisterExecutionBackend(
    const XBE_EXECUTION_BACKEND* backend);
__declspec(dllexport) int __cdecl XBE_RegisterExecutionBackendV2(
    const XBE_EXECUTION_BACKEND_V2* backend);
__declspec(dllexport) int __cdecl LoadXbe(const wchar_t* path);
__declspec(dllexport) void __cdecl UnloadXbe();

#ifdef __cplusplus
}
#endif
#endif
