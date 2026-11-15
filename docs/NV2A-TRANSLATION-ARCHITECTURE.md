# NV2A-to-Host Graphics Translation Layer

## Goal

Translate the Xbox NV2A graphics command stream and shader programs into host-GPU work. The default implementation target is the existing Windows D3D11 path. The design must not require cycle-accurate emulation of the NV2A's physical vertex-shader cores.

This is a translation/runtime architecture, not a claim of complete Xbox GPU compatibility.

## Preserve existing work

The existing NV2A decoder, HLSL generation, paired MAC/ILU semantics, PFIFO/DMA implementation, texture work, and PCI guest-memory work remain valuable. This branch builds on that foundation. The parked hardware-emulation work is not deleted or rewritten merely to make room for the translation path.

## Pipeline

1. **Guest address translation**
   - Resolve Xbox GPU-visible addresses to bounded guest-memory or local-VRAM spans.
   - Keep PCI memory and AGP/GART address spaces distinct.
   - Validate the complete requested range before exposing a host pointer.
   - Make GART/page-table translation an explicit service; never treat an AGP address as a host pointer.

2. **Command decoding**
   - Consume PFIFO DMA pushbuffer packets and decode relevant PGRAPH methods.
   - Convert state changes and draw commands into a backend-neutral graphics command/state representation.
   - Preserve ordering and synchronization boundaries.

3. **Resource translation**
   - Translate Xbox vertex/index buffers, texture layouts, surface formats, swizzled data, and sampler state into host resources.
   - Keep guest resource identity separate from host API handles.
   - Track dirty ranges and upload only when required.

4. **Vertex shader translation**
   - Reuse and strengthen the existing NV2A microcode decoder and D3D8/HLSL translator.
   - Preserve Xbox register files, constants, swizzles, masks, MAC/ILU read-before-write behavior, output routing, and final-instruction semantics.
   - Generate host shader code (initially HLSL/Shader Model 5 for D3D11).
   - Cache translated shaders by microcode plus every translation-relevant mode/state input.
   - Do not implement a cycle-by-cycle software copy of the physical vertex shader cores.

5. **Pixel/combiner translation**
   - Translate Xbox texture-stage and register-combiner behavior into host pixel-shader code.
   - Keep unsupported or unverified operations explicit; do not silently approximate them.

6. **Host backend**
   - Start with D3D11 because the repository already has a D3D11 execution path.
   - Isolate backend resource creation, shader compilation, state binding, draw submission, and readback behind a small interface so a future Vulkan backend can be added without changing Xbox decoding.

## Suggested module boundaries

Names below are architectural suggestions; inspect the current source tree before choosing exact paths or public symbols.

- Nv2aAddressSpace: bounded guest/VRAM/AGP address resolution.
- Nv2aCommandDecoder: PFIFO packet and PGRAPH method decoding.
- Nv2aPipelineState: guest-visible graphics state snapshot.
- Nv2aResourceTranslator: vertex/index/texture/surface resource conversion.
- Nv2aVertexShaderTranslator: NV2A microcode to host shader source/bytecode.
- Nv2aPixelShaderTranslator: combiners and pixel pipeline translation.
- IGraphicsBackend / D3D11GraphicsBackend: backend-neutral draw interface and D3D11 implementation.

Avoid duplicating existing classes: map these responsibilities onto the repository's existing NV2A bridge and xboxrecomp integration wherever possible.

## First implementation milestones

### M1 — Establish the translation seam
- Audit the existing NV2A bridge, pinned xboxrecomp translator, D3D11 renderer, guest-memory mapping, and tests.
- Document the actual call path from guest pushbuffer to host draw.
- Identify which existing components can be reused unchanged and which are currently hardware-model-specific.
- Add no second shader decoder unless the audit proves the existing one cannot be cleanly called.

### M2 — Backend-neutral draw packet
- Define a small, testable draw packet containing primitive type, vertex/index ranges, translated shader identities, resource bindings, and relevant fixed-function state.
- Add tests for deterministic decoding of representative draw methods.

### M3 — Vertex path end-to-end
- Resolve guest vertex data through the address-space service.
- Translate a real NV2A vertex microcode program to HLSL.
- Compile and execute it through D3D11, with deterministic input/output assertions.

### M4 — Textures and pixel pipeline
- Translate texture memory/layout/sampler state and combiner programs.
- Add offscreen render tests for each supported format/operation.

### M5 — Real XBE graphics integration
- Run a small known graphics workload through the complete guest-command-to-host-draw path.
- Capture frames and compare stable pixels or numeric tolerances.
- Report unsupported methods/features explicitly.

## Correctness rules

- Xbox semantics are the specification; host API convenience is not.
- Guest addresses are never dereferenced before checked translation.
- Preserve command ordering and guest-visible synchronization.
- Shader translation must reject malformed/reserved encodings rather than invent behavior.
- Every claimed feature needs a focused test and an end-to-end test where practical.
- Keep the hardware-emulation and translation approaches independently testable; do not delete the former to implement the latter.

## Non-goals for the first milestone

- Cycle-accurate reproduction of NV2A vertex-shader hardware.
- A second software vertex-shader execution core.
- Full Vulkan support before the D3D11 path is proven.
- Claiming a percentage of total Xbox GPU compatibility from unit-test counts alone.

## Verified repository audit (2026-11-15)

The existing code already contains important pieces of this translation path; do not build parallel replacements:

- The top-level build pins the xboxrecomp submodule and builds the NV2A module.
- The NV2A module currently links xboxrecomp's xbox_nv2a and xbox_d3d8 libraries.
- The D3D8 compatibility layer is backed by D3D11 on Windows.
- The bridge mirrors the uploaded NV2A vertex program and constants, marks shader state dirty on uploads, and documents first-draw compilation plus microcode-hash caching of the resulting host vertex shader.
- The bridge already has a vertex-program validator and an idempotent CMake patch for the pinned xboxrecomp VSH parser. The patch corrects the real NV2A DWORD1-DWORD3 field layout and preserves paired MAC/ILU read-before-write behavior.
- The bridge has both a custom FVF/DrawPrimitiveUP vertex-array route and a programmable vertex-shader route. These must be unified carefully rather than replaced blindly.
- Existing tests cover NV2A validation, HLSL generation, PFIFO/DMA parsing, vertex/index expansion, texture decoding, and several state mappings.

### Immediate gaps confirmed by the current integration notes

- The full Xbox D3D device-creation path is not yet connected to a running original XBE.
- The guest address space is not yet generally connected to NV2A MMIO/VRAM fault hooks.
- PCI-target DMA pushbuffers can use a bounded borrowed guest-physical mapping, but AGP/GART translation is still rejected.
- Vertex and texture fetches still need to be routed through a shared address-space abstraction instead of assuming local VRAM.
- Existing D3D11 rendering support is present, but there is not yet a clean backend-neutral draw packet boundary separating guest PGRAPH state from host API submission.

### First code change after this audit

Introduce a small internal translation seam around the existing implementation: a validated backend-neutral draw description and explicit guest-resource views. Keep the existing D3D8/D3D11 submission path as the first backend. The seam must not own or reinterpret raw guest pointers, and it must not duplicate the current shader parser/compiler.

## Implemented translation milestone — PCI resource views

The translation branch now has a shared `dma_get_span` resolver. It accepts a DMA object plus an object-relative byte offset and length, validates the full range, and returns a read-only host view from either:

- NV2A local VRAM, using the resolved VRAM base.
- The explicitly bound guest-physical PCI window, using the mapping-relative base produced by DMA-object resolution.

The resolver is used by:

- Vertex-array element fetch (`XboxNV2A_ReadVertexArray`).
- Linear texture decode (`XboxNV2A_DecodeTextureFromDMA`).
- Swizzled/compressed mip-chain decode (`XboxNV2A_DecodeTextureMipChainFromDMA`).
- Paletted texture and palette fetch.

This removes the earlier local-VRAM-only rejection for those resource paths while retaining strict bounds checks. AGP/GART is still rejected; it needs actual guest page-table translation and is not mapped by this helper.

Regression coverage has been added for PCI-backed vertex-array reads and linear texture decoding, including restoring the original local-VRAM DMA object/context after the test. The test is awaiting the Windows GitHub Actions result in PR #25.

## Six-part translation sprint status (2026-11-15)

1. **Unify DMA reads through one address resolver — implemented on this branch.**
   Pushbuffer parsing, vertex fetch, linear textures, mip chains, and paletted texture/palette reads now share the checked span resolver. The resolver accepts local VRAM and an explicitly bound PCI physical-memory window; it never converts guest addresses directly into host pointers.
2. **AGP/GART translation — not implemented.**
   The AGP specification deliberately leaves the concrete GART table format and control interface to the platform. A generic 4-KiB PTE assumption would be unsafe here. Implement this only after the Xbox AGP bridge's aperture, GART-base, valid-bit, and page-size behavior are identified and tested.
3. **Vertex/pixel shader coverage — existing translators reused; coverage remains partial.**
   Keep the pinned NV2A microcode-to-HLSL translator and D3D8/D3D11 combiner path. Extend operations through focused shader tests; do not add a second interpreter/compiler.
4. **PGRAPH-to-host operations — partial.**
   The bridge already maps selected render, texture-stage, combiner, surface, and draw methods. Remaining methods must be implemented from verified NV097 semantics and tested rather than silently forwarded or approximated.
5. **Backend-neutral draw packet — planned, not yet complete.**
   The current bridge still prepares and submits its custom draw directly through the D3D8 compatibility interface. Introduce a semantic packet boundary before adding another backend.
6. **Guest XBE to D3D11 rendered-frame test — not yet complete.**
   The standalone NV2A tests exercise translation components, but the original guest D3D device lifecycle and complete guest-command-to-host-draw path still need to be connected and verified with a deterministic homebrew XBE.

### Additional PCI regression coverage

The PCI-backed regression now covers vertex-array reads, a linear texture, a two-level swizzled mip chain, and palette plus paletted-texture reads. The DMA pusher itself also obtains its command stream through the same checked span resolver. Windows CI must pass on the current branch head before these newest changes are called verified.
