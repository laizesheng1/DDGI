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
private:
    std::vector<BvhNode> bvhNodes;
    std::vector<uint32_t> primitiveIndices;

public:
    /**
     * Build a binary BVH over SceneMesh world-space bounds.
     * The generated internal nodes store adjacent children at leftFirst and
     * leftFirst + 1; leaf nodes store primitive range in orderedPrimitiveIndices.
     */
    void build(const scene::Scene& scene);

    /**
     * Drop all CPU BVH nodes and primitive ordering data.
     */
    void clear();

    [[nodiscard]] const std::vector<BvhNode>& nodes() const { return bvhNodes; }
    [[nodiscard]] const std::vector<uint32_t>& orderedPrimitiveIndices() const { return primitiveIndices; }
    [[nodiscard]] bool empty() const { return bvhNodes.empty(); }
};

} // namespace rt
