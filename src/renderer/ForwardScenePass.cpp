#include "renderer/ForwardScenePass.h"

namespace renderer {

void ForwardScenePass::record(vk::CommandBuffer, const scene::Scene&)
{
    // The maintained forward fallback lives in Renderer::drawScene because it
    // shares the renderer-owned pipeline layout and material descriptor set.
    // This compatibility wrapper remains empty so older call sites can link
    // without duplicating pipeline state outside Renderer.
}

} // namespace renderer
