#pragma once

#include <vector>

#include "scene/Scene.h"

namespace rt {

struct BvhNode {
    glm::vec3 minBounds{0.0f};
    uint32_t leftFirst{0u};
    glm::vec3 maxBounds{0.0f};
    uint32_t primitiveCount{0u};
};

class SoftwareBvh {
public:
    void build(const scene::Scene& scene);
    void clear();

    [[nodiscard]] const std::vector<BvhNode>& nodes() const { return bvhNodes; }

private:
    std::vector<BvhNode> bvhNodes;
};

} // namespace rt
