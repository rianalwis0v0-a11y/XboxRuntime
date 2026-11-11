#pragma once

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) int __cdecl ModuleStart(void);
__declspec(dllexport) void __cdecl ModuleStop(void);

#ifdef __cplusplus
}
#endif
