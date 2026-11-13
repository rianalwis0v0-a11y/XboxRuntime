# Cxbx-Reloaded port plan

## Goal

Reuse proven Cxbx-Reloaded implementation code inside XboxRuntime. XboxRuntime remains the program being developed; Cxbx-Reloaded is a source-code donor, not a runtime dependency or an emulator the user must launch separately.

Original Xbox XBE files must execute from their original form. Do not add static recompilation or an XBE-to-native recompilation pipeline.

## Upstream components identified

Source repository: https://github.com/Cxbx-Reloaded/Cxbx-Reloaded

| Subsystem | Upstream source area | Porting note |
|---|---|---|
| XBE parsing/loading | src/common/xbe/Xbe.cpp, Xbe.h | The Cxbx class has UI, crypto, address-space and kernel dependencies. Port parser and mapping pieces behind XboxRuntime's loader interface rather than copying the whole class blindly. |
| Kernel exports | src/core/kernel/exports/EmuKrnl*.cpp/.h | Map export ordinals and Xbox calling conventions to XboxRuntime's guest32 dispatcher. Preserve exact Xbox ABI and semantics. |
| DirectSound | src/core/hle/DSOUND/DirectSound/ | Depends on Cxbx audio globals, XAPI patches and Windows DirectSound. Audit each function; upstream contains TODO paths and incomplete AddRef/Release implementations. |
| D3D8 HLE | src/core/hle/D3D8/ | Broad D3D8-to-host graphics translation; not a drop-in NV2A register implementation. |
| NV2A LLE | src/devices/video/ | Hardware-register/device implementation coupled to Cxbx memory, DMA, PFIFO and PGRAPH infrastructure. Port in layers. |
| Input | src/common/input/ | Adapt Xbox controller mapping and state conversion to modules/input. |
| Device / filesystem | src/devices/, src/core/kernel/exports/EmuKrnlFs.cpp | Reconcile device paths, XDVDFS/FATX and host file access with existing modules/fatx and modules/xdvdfs. |

## Import rules

1. Keep upstream copyright and GPL notices in every copied or modified source file.
2. Record source path, upstream revision, local modifications and date in this directory.
3. Include the upstream GPL-2.0 text in third_party/cxbx-reloaded/COPYING.
4. Do not change copied files to MIT.
5. Keep independently authored MIT code identifiable. A directory boundary alone does not settle whether a linked combined program is a derivative work. Before distributing a combined binary, review GPL-2.0 section 2 and obtain appropriate legal advice.
6. Do not copy files from Cxbx dependencies until their own licences are checked.
7. Do not claim a subsystem is complete until it builds and has meaningful tests.

## Initial implementation order

1. Compare Cxbx XBE header/section validation and image mapping with modules/xbe_loader and guest32. Preserve the existing launch protocol and original XBE execution.
2. Port kernel exports in small ordinal groups into guest32/kernel_compat.cpp, with ABI tests and no fake success handlers.
3. Port input state conversion.
4. Port DirectSound functionality after checking each method's implementation and host API dependencies.
5. Port NV2A device/register behavior only where it advances the existing D3D11 GPU execution path; do not silently route vertex work to CPU.
6. Integrate filesystem/device semantics and run the diagnostic XBE after each stage.

## Status

This commit adds the upstream GPL text and porting/attribution plan only. It does not claim that Cxbx code has already been integrated or that any game boots.
