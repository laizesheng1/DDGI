#pragma once

#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace ddgi {

enum class DDGIMovementType : uint32_t {
    Static = 0,
    Scrolling = 1
};

enum DDGIUpdateFlags : uint32_t {
    RelocationEnabled = 1u << 0u,
    ClassificationEnabled = 1u << 1u,
    ProbeMultiBounceEnabled = 1u << 2u
};

enum DDGIProbeState : uint32_t {
    ProbeStateActive = 0u,
    ProbeStateInactiveBackface = 1u,
    ProbeStateInactiveInsideGeometry = 2u,
    ProbeStateInactiveOutOfBounds = 3u,
    ProbeStateInactiveNoGeometry = 4u,
    ProbeStateInactiveNoLocalFrontface = 5u,
    ProbeStateInactiveOnlyBackface = 6u
};

struct DDGIVolumeDesc {
    glm::vec3 origin{-8.0f, 1.0f, -8.0f};
    glm::vec3 probeSpacing{2.0f, 2.0f, 2.0f};
    glm::uvec3 probeCounts{9u, 5u, 9u};
    uint32_t raysPerProbe{128u};
    // Interleave probe updates across this many frames. A value of 4 means
    // only one quarter of the probes trace and refresh their atlas texels each
    // frame, which reduces RT cost while hysteresis keeps lighting stable.
    uint32_t probeUpdatePhaseCount{4u};
    uint32_t irradianceOctSize{8u};
    uint32_t depthOctSize{16u};
    float hysteresis{0.97f};
    float normalBias{0.20f};
    float viewBias{0.10f};
    // Retained for SDF debug/future paths. Strict RTXGI mode does not use SDF
    // distance as a probe classification or relocation input.
    float sdfProbePushDistance{0.35f};
    // RTXGI-style stability controls for probe radiance blending. The change
    // threshold is intentionally high enough that ordinary rotated-ray noise
    // does not lower hysteresis; only real lighting discontinuities should
    // accelerate convergence. Brightness threshold limits frame-to-frame
    // brightness impulses relative to the current history, not absolute energy.
    float maxRayDistance{1000.0f};
    float irradianceGamma{5.0f};
    float distanceExponent{32.0f};
    float probeChangeThreshold{1.00f};              // 判断“是否发生大光照变化”
    float probeBrightnessThreshold{1.50f};          // 限制单次更新亮度跃迁
    // Fixed rays keep deterministic directions for classification/relocation.
    // Atlas blending skips them when possible so lighting still benefits from
    // temporally rotated probe rays.
    uint32_t fixedRayCount{16u};
    float probeBackfaceThreshold{0.25f};            // classification/relocation
    float probeMinFrontfaceDistance{0.20f};         // relocation
    // Strict RTXGI classification tests fixed frontface hits against probe
    // voxel planes one probe spacing away from the current probe position.
    float probeCellPlaneScale{1.0f};
    // Probe ray shading can feed the previous/history irradiance atlas back
    // into hit radiance. This implements diffuse multi-bounce GI over frames
    // rather than recursive same-frame ray tracing.
    bool probeMultiBounceEnabled{true};
    float probeMultiBounceIntensity{0.35f};
    float probeMultiBounceMaxRadiance{2.00f};
    bool relocationEnabled{false};
    bool classificationEnabled{true};
    DDGIMovementType movementType{DDGIMovementType::Static};
    glm::vec3 scrollAnchor{0.0f};
};

struct DDGIFrameConstants {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 volumeOriginAndRays{0.0f};
    glm::vec4 probeSpacingAndHysteresis{0.0f};
    glm::uvec4 probeCounts{0u};
    glm::vec4 biasAndDebug{0.0f};
    glm::uvec4 atlasLayout{0u};
    glm::vec4 traceParams{0.0f};
    glm::vec4 stabilityParams{0.0f};
    glm::uvec4 updateParams{0u};
    glm::vec4 scrollAnchorAndMovement{0.0f};
    glm::vec4 sdfOriginAndMaxDistance{0.0f};
    glm::vec4 sdfVoxelSizeAndClearance{0.0f};
    glm::uvec4 sdfResolutionAndFlags{0u};
    glm::vec4 multiBounceParams{0.0f};
};

enum class DebugTexture {
    Irradiance,
    Depth,
    DepthSquared
};

} // namespace ddgi
