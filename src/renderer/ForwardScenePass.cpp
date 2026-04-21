#include "renderer/ForwardScenePass.h"

namespace renderer {

void ForwardScenePass::record(vk::CommandBuffer, const scene::Scene&)
{
    // TODO: Forward fallback pass for early bring-up before GBuffer exists.
}

} // namespace renderer
