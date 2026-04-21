#include "scene/SceneGpuData.h"

namespace scene {

void SceneGpuData::create(vkm::VKMDevice* inDevice, const Scene&)
{
    device = inDevice;
    // TODO: Upload compact vertex, index, material, and instance buffers.
}

void SceneGpuData::destroy()
{
    materials.destroy();
    indices.destroy();
    vertices.destroy();
    device = nullptr;
}

} // namespace scene
