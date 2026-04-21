#include "renderer/LightingPass.h"

namespace renderer {

void LightingPass::record(vk::CommandBuffer, const ddgi::DDGIVolume&)
{
    // TODO: Full-screen lighting pass with DDGI irradiance lookup.
}

} // namespace renderer
