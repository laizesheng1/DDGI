#include "rt/SoftwareBvh.h"

#include <algorithm>
#include <cfloat>
#include <numeric>

namespace rt {
namespace {

struct BuildRange {
    uint32_t first{0u};
    uint32_t count{0u};
};

glm::vec3 boundsExtent(const BvhNode& node)
{
    return node.maxBounds - node.minBounds;
}

uint32_t largestAxis(const glm::vec3& extent)
{
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0u;
    }
    if (extent.y >= extent.z) {
        return 1u;
    }
    return 2u;
}

float axisValue(const glm::vec3& value, uint32_t axis)
{
    return axis == 0u ? value.x : (axis == 1u ? value.y : value.z);
}

void expandBounds(BvhNode& node, const scene::SceneMesh& mesh)
{
    node.minBounds = glm::min(node.minBounds, mesh.minBounds);
    node.maxBounds = glm::max(node.maxBounds, mesh.maxBounds);
}

BvhNode calculateBounds(const std::vector<scene::SceneMesh>& meshes,
                        const std::vector<uint32_t>& primitiveIndices,
                        BuildRange range)
{
    BvhNode bounds{};
    bounds.minBounds = glm::vec3(FLT_MAX);
    bounds.maxBounds = glm::vec3(-FLT_MAX);

    for (uint32_t primitiveOffset = 0; primitiveOffset < range.count; ++primitiveOffset) {
        const uint32_t primitiveIndex = primitiveIndices[range.first + primitiveOffset];
        expandBounds(bounds, meshes[primitiveIndex]);
    }
    return bounds;
}

BvhNode calculateCentroidBounds(const std::vector<scene::SceneMesh>& meshes,
                                const std::vector<uint32_t>& primitiveIndices,
                                BuildRange range)
{
    BvhNode bounds{};
    bounds.minBounds = glm::vec3(FLT_MAX);
    bounds.maxBounds = glm::vec3(-FLT_MAX);

    for (uint32_t primitiveOffset = 0; primitiveOffset < range.count; ++primitiveOffset) {
        const uint32_t primitiveIndex = primitiveIndices[range.first + primitiveOffset];
        const glm::vec3 centroid = meshes[primitiveIndex].centroid;
        bounds.minBounds = glm::min(bounds.minBounds, centroid);
        bounds.maxBounds = glm::max(bounds.maxBounds, centroid);
    }
    return bounds;
}

void buildNode(const std::vector<scene::SceneMesh>& meshes,
               std::vector<uint32_t>& primitiveIndices,
               std::vector<BvhNode>& nodes,
               uint32_t nodeIndex,
               BuildRange range)
{
    BvhNode node = calculateBounds(meshes, primitiveIndices, range);
    if (range.count <= 2u) {
        node.leftFirst = range.first;
        node.primitiveCount = range.count;
        nodes[nodeIndex] = node;
        return;
    }

    const BvhNode centroidBounds = calculateCentroidBounds(meshes, primitiveIndices, range);
    const uint32_t splitAxis = largestAxis(boundsExtent(centroidBounds));
    const uint32_t splitMiddle = range.first + range.count / 2u;

    auto begin = primitiveIndices.begin() + range.first;
    auto middle = primitiveIndices.begin() + splitMiddle;
    auto end = primitiveIndices.begin() + range.first + range.count;

    std::nth_element(begin, middle, end, [&](uint32_t lhs, uint32_t rhs) {
        return axisValue(meshes[lhs].centroid, splitAxis) < axisValue(meshes[rhs].centroid, splitAxis);
    });

    const uint32_t leftChildIndex = static_cast<uint32_t>(nodes.size());
    const uint32_t rightChildIndex = leftChildIndex + 1u;
    nodes.push_back({});
    nodes.push_back({});

    // Internal nodes point at the first child. Children are appended
    // contiguously before either subtree is expanded, so traversal can read
    // leftFirst and leftFirst + 1 without storing a second child index.
    node.leftFirst = leftChildIndex;
    node.primitiveCount = 0u;
    nodes[nodeIndex] = node;

    buildNode(meshes, primitiveIndices, nodes, leftChildIndex, BuildRange{range.first, splitMiddle - range.first});
    buildNode(meshes, primitiveIndices, nodes, rightChildIndex, BuildRange{splitMiddle, range.first + range.count - splitMiddle});
}

} // namespace

void SoftwareBvh::build(const scene::Scene& scene)
{
    clear();

    const std::vector<scene::SceneMesh>& meshes = scene.meshes();
    if (meshes.empty()) {
        return;
    }

    primitiveIndices.resize(meshes.size());
    std::iota(primitiveIndices.begin(), primitiveIndices.end(), 0u);
    bvhNodes.reserve(meshes.size() * 2u);
    bvhNodes.push_back({});
    buildNode(meshes, primitiveIndices, bvhNodes, 0u, BuildRange{0u, static_cast<uint32_t>(meshes.size())});
}

void SoftwareBvh::clear()
{
    primitiveIndices.clear();
    bvhNodes.clear();
}

} // namespace rt
