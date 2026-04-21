#pragma once

#include "scene/Scene.h"

namespace renderer {

class ForwardScenePass {
public:
    void record(vk::CommandBuffer commandBuffer, const scene::Scene& scene);
};

} // namespace renderer
