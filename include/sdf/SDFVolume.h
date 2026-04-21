#pragma once

#include "Texture.h"
#include "VKMDevice.h"

namespace sdf {

struct SDFVolumeDesc {
    glm::vec3 origin{0.0f};
    glm::vec3 voxelSize{0.25f};
    glm::uvec3 resolution{64u};
};

class SDFVolume {
public:
    void create(vkm::VKMDevice* device, const SDFVolumeDesc& desc);
    void destroy();

    [[nodiscard]] const SDFVolumeDesc& description() const { return desc; }

private:
    vkm::VKMDevice* device{nullptr};
    SDFVolumeDesc desc{};
    vkm::Texture sdfTexture{};
};

} // namespace sdf
