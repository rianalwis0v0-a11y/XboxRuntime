# Cxbx-Reloaded port plan

## Goal

Reuse proven Cxbx-Reloaded implementation code inside XboxRuntime. XboxRuntime remains the program being developed; Cxbx-Reloaded is a source-code donor, not a runtime dependency or an emulator the user must launch separately.

Original Xbox XBE files must execute from their original form. Do not add static recompilation or an XBE-to-native recompilation pipeline.

## Mixed licensing

The intended source layout is mixed-licence:

- Independently authored, non-derivative XboxRuntime code remains MIT where the relevant copyright holders have granted those rights.
- Copied or adapted Cxbx-Reloaded code retains its upstream GPL-2.0 (or exact per-file) licence and notices.
- Do not relabel Cxbx-derived code as MIT.
- A combined executable/distribution may be subject to GPL requirements. Keeping files in separate directories does not itself settle that issue.
- Keep the root MIT licence for eligible original code for now. Do not replace it or claim dual-licensing rights for contributors without confirming permission from every relevant rights holder.

This is a source-file licensing plan, not legal advice. Review the final combined-work and distribution obligations before release.

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

1. **XBE loader — first adaptation committed:** guest32/guest32.cpp now follows Cxbx's XBE metadata-loading flow for header, certificate, section table, section names, library-version and TLS ranges; maps raw sections and zero-fills virtual tails; selects the retail/debug entry-point XOR from the kernel thunk; validates executable entry-point membership; and applies bounded section page protections. The named-pipe guest launch/release protocol is unchanged. This code is marked GPL-2.0-or-later and attributed in third_party/cxbx-reloaded/README.md. The Cxbx UI, crypto, and VM manager were not copied.
2. Port kernel exports in small ordinal groups into guest32/kernel_compat.cpp, with ABI tests and no fake success handlers.
3. Port input state conversion.
4. Port DirectSound functionality after checking each method's implementation and host API dependencies.
5. Port NV2A device/register behavior only where it advances the existing D3D11 GPU execution path; do not silently route vertex work to CPU.
6. Integrate filesystem/device semantics and run the diagnostic XBE after each stage.

## Status

This PR now includes an initial Cxbx-derived XBE loader adaptation in addition to the licence and porting plan. The Windows build and diagnostic XBE tests have not yet been run for this commit; no game boot or compatibility improvement is claimed until CI passes.
