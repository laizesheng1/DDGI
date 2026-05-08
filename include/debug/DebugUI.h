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
    bool showProbeRadianceStats{false};         //比较消耗绘制性能
    bool showProbeStatusStats{true};
    bool autoFitProbesToSceneBounds{true};
    bool relocationEnabled{false};              //每个当前 phase 的 probe 会根据 fixed rays 里的 backface hit 来移动自己的 local offset
    bool classificationEnabled{true};           //每个 probe 会用当前 phase 里刚 trace 出来的 fixed ray 数据判断自己是不是应该 inactive，把可能在几何体里的 probe 从 lighting 和后续 trace 中里剔掉
    float probeDensity{1.0f};
    float hysteresis{0.97f};                    //新旧帧的混合比例
    float maxRayDistance{1000.0f};
    uint32_t raysPerProbe{128u};
    uint32_t fixedRayCount{16u};                // nums of ray which don't FibonacciDirection in ddgi_trace.rgen
    ProbeDistributionMode distributionMode{ProbeDistributionMode::UniformInSceneBounds};
    glm::vec3 volumeOffset{0.0f};
    glm::vec3 volumeOrigin{0.0f};
    glm::vec3 averageProbeRadiance{0.0f};
    glm::vec3 maxProbeRadiance{0.0f};
    uint32_t probeCount{0u};
    uint32_t inactiveProbeCount{0u};
    uint32_t activeProbeCount{0u};
    uint32_t inactiveBackfaceCount{0u};
    uint32_t inactiveNoGeometryCount{0u};
    uint32_t inactiveNoLocalFrontfaceCount{0u};
    uint32_t inactiveOnlyBackfaceCount{0u};
    uint32_t probeUpdatePhaseCount{4u};
    uint32_t currentProbeUpdatePhase{0u};
    uint32_t radianceDebugProbeCount{0u};
    bool requestApplyProbeLayout{false};
    bool requestClearProbes{false};
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
