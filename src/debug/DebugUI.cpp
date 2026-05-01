#include "debug/DebugUI.h"

#include <array>
#include <format>

namespace debug {
namespace {

constexpr std::array<uint32_t, 5> kRaysPerProbeOptions{16u, 32u, 64u, 128u, 256u};

int32_t raysPerProbeOptionIndex(uint32_t raysPerProbe)
{
    for (size_t optionIndex = 0; optionIndex < kRaysPerProbeOptions.size(); ++optionIndex) {
        if (kRaysPerProbeOptions[optionIndex] == raysPerProbe) {
            return static_cast<int32_t>(optionIndex);
        }
    }
    return 3;
}

} // namespace
void DebugUI::draw(vkm::HUD* ui, DebugUIState& state)
{
    if (ui == nullptr) {
        return;
    }

    if (ui->header("DDGI Debug")) {
        ui->checkBox("Enable DDGI", &state.enableDdgi);
        ui->checkBox("Show probe spheres", &state.showProbeSpheres);
        ui->checkBox("Show atlas window", &state.showAtlasWindow);
        ui->checkBox("Show radiance stats", &state.showProbeRadianceStats);
        ui->checkBox("Auto fit scene bounds", &state.autoFitProbesToSceneBounds);
        ui->sliderFloat("Probe density", &state.probeDensity, 0.25f, 3.0f);

        int32_t raysPerProbeOption = raysPerProbeOptionIndex(state.raysPerProbe);
        if (ui->comboBox("Rays per probe", &raysPerProbeOption, {"16", "32", "64", "128", "256"})) {
            state.raysPerProbe = kRaysPerProbeOptions[static_cast<size_t>(raysPerProbeOption)];
        }

        int32_t distributionMode = static_cast<int32_t>(state.distributionMode);
        if (ui->comboBox("Probe distribution", &distributionMode, {"UniformInSceneBounds", "ManualVolume"})) {
            state.distributionMode = static_cast<ProbeDistributionMode>(distributionMode);
        }
        if (ui->button("Apply probe layout")) {
            state.requestApplyProbeLayout = true;
        }

        // The bundled HUD wrapper only exposes interactive widgets. Reusing a
        // button as a static label keeps the current probe summary visible
        // without reaching into vulkan_base to add a new text API.
        const std::string probeCountLabel = std::format("Probe count: {}", state.probeCount);
        ui->button(probeCountLabel.c_str());

        const std::string raysLabel = std::format("Rays / probe: {}", state.raysPerProbe);
        ui->button(raysLabel.c_str());

        const std::string phaseLabel = std::format(
            "Update phase: {}/{}",
            state.currentProbeUpdatePhase,
            state.probeUpdatePhaseCount);
        ui->button(phaseLabel.c_str());

        const std::string originLabel = std::format(
            "Volume origin: {:.2f}, {:.2f}, {:.2f}",
            state.volumeOrigin.x,
            state.volumeOrigin.y,
            state.volumeOrigin.z);
        ui->button(originLabel.c_str());

        if (state.showProbeRadianceStats) {
            const std::string avgRadianceLabel = std::format(
                "Avg radiance RGB: {:.3f}, {:.3f}, {:.3f}",
                state.averageProbeRadiance.x,
                state.averageProbeRadiance.y,
                state.averageProbeRadiance.z);
            ui->button(avgRadianceLabel.c_str());

            const std::string maxRadianceLabel = std::format(
                "Max radiance RGB: {:.3f}, {:.3f}, {:.3f}",
                state.maxProbeRadiance.x,
                state.maxProbeRadiance.y,
                state.maxProbeRadiance.z);
            ui->button(maxRadianceLabel.c_str());

            const std::string sampleCountLabel = std::format(
                "Radiance samples: {} probes",
                state.radianceDebugProbeCount);
            ui->button(sampleCountLabel.c_str());
        }

        // The user's current workflow is about moving the whole probe volume,
        // not editing individual probes. These three fields therefore apply a
        // translation to the generated DDGI volume origin before the layout is
        // rebuilt.
        ui->inputFloat("Volume offset X", &state.volumeOffset.x, 0.10f, 3);
        ui->inputFloat("Volume offset Y", &state.volumeOffset.y, 0.10f, 3);
        ui->inputFloat("Volume offset Z", &state.volumeOffset.z, 0.10f, 3);
    }
}

} // namespace debug
