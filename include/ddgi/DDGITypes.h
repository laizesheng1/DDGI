#pragma once

#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace ddgi {

struct DDGIVolumeDesc {
    glm::vec3 origin{-8.0f, 1.0f, -8.0f};
    glm::vec3 probeSpacing{2.0f, 2.0f, 2.0f};
    glm::uvec3 probeCounts{9u, 5u, 9u};
    uint32_t raysPerProbe{128u};
    uint32_t irradianceOctSize{8u};
    uint32_t depthOctSize{16u};
    float hysteresis{0.97f};
    float normalBias{0.20f};
    float viewBias{0.10f};
    float sdfProbePushDistance{0.35f};
};

struct DDGIFrameConstants {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 volumeOriginAndRays{0.0f};
    glm::vec4 probeSpacingAndHysteresis{0.0f};
    glm::uvec4 probeCounts{0u};
    glm::vec4 biasAndDebug{0.0f};
};

enum class DebugTexture {
    Irradiance,
    Depth,
    DepthSquared
};

} // namespace ddgi
