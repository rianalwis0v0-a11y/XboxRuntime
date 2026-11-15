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
