#include "ddgi/DDGIResources.h"

namespace ddgi {

void DDGIResources::create(vkm::VKMDevice* inDevice, const DDGIVolumeDesc& desc)
{
    device = inDevice;

    const uint32_t probeCountX = desc.probeCounts.x;
    const uint32_t probeCountY = desc.probeCounts.y;
    const uint32_t probeCountZ = desc.probeCounts.z;
    const uint32_t probeCount = probeCountX * probeCountY * probeCountZ;

    textureSet.irradianceExtent.width = probeCount * (desc.irradianceOctSize + 2u);
    textureSet.irradianceExtent.height = desc.irradianceOctSize + 2u;
    textureSet.depthExtent.width = probeCount * (desc.depthOctSize + 2u);
    textureSet.depthExtent.height = desc.depthOctSize + 2u;

    // Texture allocation is intentionally deferred to the first DDGI resource pass.
    // The extents are available now so debug UI and descriptor layouts can be wired first.
    created = (device != nullptr);
}

void DDGIResources::destroy()
{
    if (!created) {
        return;
    }

    textureSet.irradiance.destroy();
    textureSet.depth.destroy();
    textureSet.depthSquared.destroy();
    textureSet = {};
    device = nullptr;
    created = false;
}

} // namespace ddgi
