#pragma once

#include "ddgi/DDGITypes.h"
#include "VKMDevice.h"
#include "Texture.h"
#include "vulkan/vulkan.hpp"

namespace ddgi {

struct DDGITextureSet {
    vkm::Texture2D irradiance;
    vkm::Texture2D depth;
    vkm::Texture2D depthSquared;

    vk::Extent2D irradianceExtent{};
    vk::Extent2D depthExtent{};
};

class DDGIResources {
public:
    void create(vkm::VKMDevice* device, const DDGIVolumeDesc& desc);
    void destroy();

    [[nodiscard]] bool isCreated() const { return created; }
    [[nodiscard]] const DDGITextureSet& textures() const { return textureSet; }

private:
    vkm::VKMDevice* device{nullptr};
    DDGITextureSet textureSet{};
    bool created{false};
};

} // namespace ddgi
