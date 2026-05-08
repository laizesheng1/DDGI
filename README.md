# VK MiniRender DDGI

VK MiniRender DDGI is a Vulkan Ray Tracing sample built on the existing `src/vulkan_base` framework. The DDGI implementation lives in the app, scene, rt, ddgi, renderer, debug, sdf, and shader modules; `vulkan_base` remains a reusable platform layer for device, swapchain, buffers, textures, HUD, and glTF loading.

Primary references:

- [NVIDIA RTXGI-DDGI](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [RTXGI DDGIVolume Reference](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/DDGIVolume.md)
- [RTXGI Integration Guide](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Integration.md)

## Module Responsibilities

| Path | Responsibility |
|---|---|
| `src/vulkan_base/` | Existing Vulkan framework; not used for DDGI algorithm ownership |
| `include/app` / `src/app` | Sample lifetime, frame recording, HUD state, DDGI setting application |
| `include/scene` / `src/scene` | glTF scene, Scene AABB, RT compact geometry, material factors, RT texture descriptors |
| `include/rt` / `src/rt` | RT feature checks, BLAS/TLAS, SBT, scene binding |
| `include/ddgi` / `src/ddgi` | DDGI volume, resources, pipelines, trace/update/classify/relocate/SDF metadata passes |
| `include/renderer` / `src/renderer` | GBuffer, fullscreen DDGI lighting, forward fallback |
| `include/debug` / `src/debug` | HUD, probe sphere visualization, atlas window, probe stats |
| `include/sdf` / `src/sdf` | GPU global unsigned distance field, surface voxelization, 3D Jump Flood generation, probe placement support |
| `shaders/glsl` | Scene, lighting, DDGI, RT, and debug shaders |

## Frame Flow

```text
ddgiVolume.updateConstants(camera, frameCounter)
ddgiVolume.updateProbesFromSDF(commandBuffer, sdfVolume)
ddgiVolume.traceProbeRays(commandBuffer, rayTracing.sceneBinding())
ddgiVolume.updateProbes(commandBuffer)

renderer.recordGBuffer(commandBuffer, scene, camera, extent)

begin main render pass
renderer.drawScene(commandBuffer, scene, camera, extent, &ddgiVolume, enableDdgi)
probeVisualizer.draw(...)
drawUI(...)
end main render pass

atlasWindow.update(ddgiVolume, showAtlasWindow)
```

## Implemented DDGI Features

- Hardware RT probe tracing with `vkCmdTraceRaysKHR`, TLAS/BLAS, SBT, raygen/miss/any-hit/closest-hit shaders.
- RT material shading from compact scene buffers plus per-material texture arrays:
  - baseColor factor and texture
  - normal map
  - metallic-roughness texture and factors
  - emissive factor and texture
  - alpha mask / alpha cutoff via any-hit `ignoreIntersectionEXT`
  - KHR_texture_transform scale/offset when exposed by tinygltf
- Probe ray data stores radiance, distance, distance squared, ray direction, shading normal, and hit/frontface/backface flags.
- Fixed rays are deterministic and are the only geometry evidence used by strict RTXGI-style classification/relocation; rotated non-fixed rays feed irradiance and moments blending.
- Inactive probes keep fixed-ray tracing but skip non-fixed rays, reducing RT work without deleting geometry evidence.
- Irradiance atlas uses octahedral tiles with a one-texel border and gamma-encoded history blending.
- Distance visibility uses first and second moments in separate atlas images, with Chebyshev visibility in the lighting gather.
- Classification follows the RTXGI two-phase fixed-ray test: backface-heavy probes become inactive, then probes are active only when a fixed frontface hit is proven to lie inside the current probe voxel/cell.
- Relocation follows the RTXGI fixed-ray path by comparing closest backface, closest frontface, and farthest frontface evidence, writing offsets only inside the 45% probe-cell relocation bound.
- Lighting pass performs 8-probe trilinear gather, normal weighting, inactive-probe skip, and moment-based visibility.
- Clear history resets probe ray data, offsets, states, and atlases before the next update.
- Scene fitting derives probe count, spacing, and origin from the loaded scene AABB.
- GPU SDF generation builds a global unsigned scene distance field with surface seed voxelization, 3D Jump Flood propagation, and final R32F distance output.
- SDF resources use separate 3D images for surface seeds, JFA ping/pong seeds, and final distance. Images stay in `General` layout so compute/debug passes can write or inspect the generated field without descriptor churn.
- In strict RTXGI mode the SDF is debug/future data only. The pre-trace metadata pass no longer samples SDF distance, no longer pushes probe offsets from the SDF gradient, and never writes probe state; it only clamps externally edited offsets to the RTXGI 45% cell bound.
- Probe debug spheres read the real probe offset/state buffers and color active/inactive probes differently.
- Independent atlas window displays irradiance, depth, and depth-squared resources.

## RTXGI Alignment

This project follows RTXGI DDGIVolume semantics for the main single-volume path:

- DDGI resources are separated into probe ray data, irradiance atlas, distance/moment atlas data, probe offsets/states, constants, and debug-visible metadata.
- Probe tracing separates fixed rays from temporally rotated rays.
- Classification and relocation use deterministic fixed rays to avoid phase-driven flip-flop. Inactive probes still trace fixed rays, while non-fixed rays are skipped for inactive probes.
- Atlas updates use hysteresis, gamma handling, brightness clamping, change-threshold acceleration, and border copies for filtered sampling.
- Lighting gather uses probe cage interpolation, surface bias, inactive probe rejection, and moment visibility.
- SDF is not part of strict RTXGI probe classification/relocation. Inside/backface and local-geometry decisions are fixed-ray and RT-driven.

## Project-Level Differences

- The sample currently targets one DDGI volume. Multi-volume blending is outside the current renderer flow.
- Distance moments are stored as two R32F atlas images instead of one packed RG texture. This keeps compatibility with the existing atlas visualizer and descriptor layout while preserving the same mean/second-moment semantics.
- Infinite scrolling state is present in CPU/shader constants, but the sample still rebuilds or moves the volume through settings rather than doing production RTXGI plane rolling with atlas history remapping.
- Variability-driven update suppression is not yet a separate GPU resource path; update work is currently controlled by fixed rays, classification, inactive-probe skipping, and phase interleaving.
- The SDF is a global unsigned distance field generated by surface voxelization plus 3D Jump Flood, not Lumen-style per-mesh signed distance fields or a watertight scene sign solver. It is retained for debug/future probe-placement experiments, but strict RTXGI classification/relocation ignores it.
- Empty cells and cells without local fixed-ray frontface evidence are expected to become inactive. If a large open region loses too many active probes, increase probe density, reduce probe spacing, or adjust volume placement instead of relaxing the classifier.
- Async compute queue ownership is not enabled because the existing framework exposes a single graphics/transfer queue path; barriers are explicit inside that queue.

## Debug Controls

The HUD exposes DDGI enable, probe spheres, atlas window, radiance stats, auto-fit, relocation, classification, probe density, history weight, max ray distance, rays per probe, fixed rays, update phases, distribution mode, apply settings, clear history, current phase, inactive probe count, volume origin, radiance stats, and volume offset.

## Validation

Useful checks:

1. Toggle DDGI and compare indirect diffuse contribution in Sponza.
2. Use textured/alpha-masked assets and verify probe radiance follows baseColor/emissive textures and cutout alpha.
3. Toggle classification/relocation and watch inactive probe count converge in a static scene; sparse empty cells becoming inactive is expected in strict RTXGI mode.
4. Clear probe history and confirm atlas values reconverge.
5. Open the atlas window and inspect irradiance/depth/depth-squared updates.
6. Enable probe spheres and confirm active/inactive colors and relocated positions match real probe metadata.
7. Enable relocation near walls/columns and confirm offsets come from fixed-ray backface/frontface evidence, not SDF distance.

Shader sources under `shaders/glsl/**` are compiled by the `Shaders` CMake target into `.spv` outputs.
