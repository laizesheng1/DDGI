#include "rt/SoftwareBvh.h"

namespace rt {

void SoftwareBvh::build(const scene::Scene&)
{
    bvhNodes.clear();
    // TODO: Build a CPU BVH and upload it for compute-shader ray traversal.
}

void SoftwareBvh::clear()
{
    bvhNodes.clear();
}

} // namespace rt
