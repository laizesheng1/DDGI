#pragma once

#include "HUD.h"
#include "ddgi/DDGITypes.h"

namespace debug {

struct DebugUIState {
    bool enableDdgi{true};
    bool showProbes{true};
    bool showTexturePanel{true};
    ddgi::DebugTexture selectedTexture{ddgi::DebugTexture::Irradiance};
};

class DebugUI {
public:
    void draw(vkm::HUD* ui, DebugUIState& state);
};

} // namespace debug
