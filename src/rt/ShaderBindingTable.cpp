#include "rt/ShaderBindingTable.h"

namespace rt {

void ShaderBindingTable::create(vkm::VKMDevice* inDevice)
{
    device = inDevice;
}

void ShaderBindingTable::destroy()
{
    hit.destroy();
    miss.destroy();
    raygen.destroy();
    device = nullptr;
}

} // namespace rt
