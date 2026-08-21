# VK MiniRender DDGI

VK MiniRender DDGI is a Windows Vulkan sample for experimenting with Dynamic Diffuse Global Illumination (DDGI) on hardware ray-tracing capable GPUs. It builds the DDGI renderer around the reusable `src/vulkan_base` framework, which owns the Vulkan device, swapchain, resource helpers, HUD, and glTF loading infrastructure.

The project is a learning-oriented renderer rather than a packaged SDK. Its current focus is a single DDGI volume, ray-traced probe updates, deferred PBR lighting, and inspectable GPU resources.

## Project structure

| Directory | Purpose |
|---|---|
| `assets/` | glTF models and other runtime assets. |
| `include/` | Public interfaces for the application, scene, RT, DDGI, renderer, debug, and SDF modules. |
| `src/` | Implementation of the sample. `src/vulkan_base/` is the reusable Vulkan framework; the other modules implement project-specific rendering features. |
| `shaders/glsl/` | GLSL source for scene rendering, DDGI, ray tracing, lighting, SDF, and debug passes. CMake compiles these files to SPIR-V. |
| `external/` | Third-party source and binary dependencies used by the sample. |
| `libs/` | Bundled platform libraries, including the Windows Vulkan fallback used by CMake when needed. |
| `harness-template/` | DDGI-oriented engineering template containing architecture, coding, verification, and handoff guidance. |
| `build/` | Generated CMake build tree; do not edit manually. |
| `bin/` | Generated executables and runtime output. |

## Getting up and running

### Prerequisites

- Windows with a Vulkan ray-tracing capable GPU and a current Vulkan runtime/driver.
- CMake 3.15 or newer.
- A C++23-capable compiler. Visual Studio 2022 is the tested multi-configuration workflow.
- Vulkan SDK tools available on `PATH`: `glslc` and `spirv-val`.

### Build and run

From the repository root, configure and build the Release configuration:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\bin\Release\VK_DDGI.exe
```

`VK_DDGI` depends on the `Shaders` target. Every supported shader stage under `shaders/glsl/` is compiled for Vulkan 1.3 and validated with `spirv-val` as part of the build.

## Features

### Scene and ray tracing

- glTF scene loading with compact GPU geometry and material data for rasterization and ray tracing.
- `KHR_lights_punctual` directional, point, and spot lights, with a fallback directional light for scenes that do not provide one.
- BLAS/TLAS construction, shader binding tables, and ray generation, miss, any-hit, and closest-hit shaders for probe tracing.
- Textured base color, normal, metallic-roughness, emissive, alpha-mask, and texture-transform support in the compact RT material path.

### DDGI

- A single DDGI volume with deterministic fixed rays and phase-interleaved rotated rays.
- Probe-ray radiance, irradiance, distance, and distance-squared GPU resources with temporal atlas updates and border copies.
- Moment-based visibility, trilinear probe gathering, surface bias, inactive-probe filtering, and diffuse multi-bounce feedback through atlas history.
- Fixed-ray probe classification and relocation using backface and local-frontface evidence.
- Probe-state/offset visualization, irradiance and distance atlas inspection, history reset, scene-AABB fitting, and runtime tuning through the HUD.

### Rendering and debug tools

- Deferred PBR GBuffer rendering with Cook-Torrance metallic-roughness direct lighting, emissive output, and DDGI diffuse indirect lighting.
- Optional 2048×2048 rasterized shadow map for the main directional light, with PCF and receiver bias controls.
- Lighting debug views for direct lighting, shadow visibility, shadow depth, receiver depth, and shadow-map coordinate diagnostics.

### SDF

- GPU generation of a global unsigned distance field using surface voxelization and 3D Jump Flood propagation.
- The SDF is a planned project capability for probe placement and spatial queries. It is generated and retained as GPU data, but is not yet consumed by probe placement, classification, or relocation.

## Frame flow

```text
Update DDGI constants and probe metadata
        ↓
Trace probe rays against the scene TLAS
        ↓
Update DDGI irradiance and distance atlases
        ↓
Record shadow map and GBuffer
        ↓
Run fullscreen PBR + DDGI lighting
        ↓
Draw probe/debug UI and update atlas inspection views
```

## Current boundaries

- The renderer currently evaluates one DDGI volume; multi-volume selection and blending are not implemented.
- Probe work is phase-interleaved. Fixed rays supply stable classification and relocation evidence, while non-fixed rays supply irradiance and distance blending.
- The shadow system is one rasterized directional-light shadow map. Cascades, point/spot shadows, and RT visibility rays for probe light injection are not implemented.
- The SDF is unsigned and global. It is not a per-mesh signed-distance-field system and has not yet been connected to DDGI placement or classification policy.
- Infinite scrolling and variability-driven DDGI update scheduling are outside the current renderer flow.
- The framework currently uses its single graphics/transfer queue path; async-compute ownership is not implemented.

## Validation

After a successful build, use the included sample scenes and HUD to verify the main paths:

1. Toggle DDGI to isolate diffuse indirect lighting, then clear probe history and observe reconvergence.
2. Toggle classification and relocation while visualizing probe states and offsets around walls or columns.
3. Inspect irradiance, depth, and depth-squared atlases in the atlas window.
4. Toggle the directional shadow map and use the lighting debug views to inspect visibility, stored depth, receiver depth, and projection coordinates.
5. Load textured or alpha-masked assets and confirm both raster and probe-tracing paths respect their authored material data.

## References

- [NVIDIA RTXGI](https://github.com/NVIDIA-RTX/RTXGI) provides the repository structure and documentation style used as inspiration here. Its current v2 repository focuses on NRC and SHaRC, and identifies DDGI as an RTXGI v1.x component.
- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI) is the DDGI v1 reference implementation.
- [DDGIVolume reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md) documents probe resources, classification, relocation, and tuning semantics.
- [Integration guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md) describes the DDGI runtime integration sequence.
