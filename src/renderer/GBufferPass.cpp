#include "renderer/GBufferPass.h"

namespace renderer {

void GBufferPass::record(vk::CommandBuffer, const scene::Scene&)
{
    // TODO: Render albedo, normal, roughness/metallic, depth.
}

} // namespace renderer
