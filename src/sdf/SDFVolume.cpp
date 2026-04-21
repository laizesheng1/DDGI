#include "sdf/SDFVolume.h"

namespace sdf {

void SDFVolume::create(vkm::VKMDevice* inDevice, const SDFVolumeDesc& inDesc)
{
    device = inDevice;
    desc = inDesc;
    // TODO: Allocate a 3D SDF texture or a brick atlas.
}

void SDFVolume::destroy()
{
    sdfTexture.destroy();
    device = nullptr;
}

} // namespace sdf
