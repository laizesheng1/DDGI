#include "debug/DebugUI.h"

namespace debug {

void DebugUI::draw(vkm::HUD* ui, DebugUIState& state)
{
    if (ui == nullptr) {
        return;
    }

    if (ui->header("DDGI Debug")) {
        ui->checkBox("Enable DDGI", &state.enableDdgi);
        ui->checkBox("Show probes", &state.showProbes);
        ui->checkBox("Show texture panel", &state.showTexturePanel);

        int32_t selected = static_cast<int32_t>(state.selectedTexture);
        if (ui->comboBox("Texture", &selected, {"Irradiance", "Depth", "Depth squared"})) {
            state.selectedTexture = static_cast<ddgi::DebugTexture>(selected);
        }
    }
}

} // namespace debug
