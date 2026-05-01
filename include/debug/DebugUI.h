#pragma once

#include "glm/glm.hpp"
#include "HUD.h"
#include "ddgi/DDGITypes.h"

namespace debug {

enum class ProbeDistributionMode {
    UniformInSceneBounds = 0,
    ManualVolume = 1
};

struct DebugUIState {
    bool enableDdgi{true};
    bool showProbeSpheres{true};
    bool showAtlasWindow{true};
    bool showProbeRadianceStats{true};
    bool autoFitProbesToSceneBounds{true};
    float probeDensity{1.0f};
    uint32_t raysPerProbe{128u};
    ProbeDistributionMode distributionMode{ProbeDistributionMode::UniformInSceneBounds};
    glm::vec3 volumeOffset{0.0f};
    glm::vec3 volumeOrigin{0.0f};
    glm::vec3 averageProbeRadiance{0.0f};
    glm::vec3 maxProbeRadiance{0.0f};
    uint32_t probeCount{0u};
    uint32_t probeUpdatePhaseCount{4u};
    uint32_t currentProbeUpdatePhase{0u};
    uint32_t radianceDebugProbeCount{0u};
    bool requestApplyProbeLayout{false};
};

class DebugUI {
public:
    /**
     * Draw DDGI debug controls into the existing HUD. The HUD wrapper does not
     * expose a generic read-only text widget, so a couple of one-way values are
     * presented via inert buttons to keep the debug surface compact without
     * modifying vulkan_base.
     */
    void draw(vkm::HUD* ui, DebugUIState& state);
};

} // namespace debug
