#include "debug/DebugUI.h"

#include <algorithm>
#include <array>
#include <format>

namespace debug {
namespace {

constexpr std::array<uint32_t, 5> kRaysPerProbeOptions{16u, 32u, 64u, 128u, 256u};
constexpr std::array<uint32_t, 4> kUpdatePhaseOptions{1u, 2u, 4u, 8u};

int32_t raysPerProbeOptionIndex(uint32_t raysPerProbe)
{
    for (size_t optionIndex = 0; optionIndex < kRaysPerProbeOptions.size(); ++optionIndex) {
        if (kRaysPerProbeOptions[optionIndex] == raysPerProbe) {
            return static_cast<int32_t>(optionIndex);
        }
    }
    return 3;
}

int32_t updatePhaseOptionIndex(uint32_t phaseCount)
{
    for (size_t optionIndex = 0; optionIndex < kUpdatePhaseOptions.size(); ++optionIndex) {
        if (kUpdatePhaseOptions[optionIndex] == phaseCount) {
            return static_cast<int32_t>(optionIndex);
        }
    }
    return 2;
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
        ui->checkBox("Show probe status", &state.showProbeStatusStats);
        ui->checkBox("Auto fit scene bounds", &state.autoFitProbesToSceneBounds);
        ui->checkBox("Relocation", &state.relocationEnabled);
        ui->checkBox("Classification", &state.classificationEnabled);
        ui->checkBox("Probe multi-bounce", &state.probeMultiBounceEnabled);
        ui->sliderFloat("Probe density", &state.probeDensity, 0.25f, 3.0f);
        ui->sliderFloat("DDGI intensity", &state.ddgiIntensity, 0.0f, 5.0f);
        ui->sliderFloat("History weight", &state.hysteresis, 0.0f, 0.99f);
        ui->sliderFloat("Multi-bounce intensity", &state.probeMultiBounceIntensity, 0.0f, 1.5f);
        ui->inputFloat("Max ray distance", &state.maxRayDistance, 1.0f, 1);

        int32_t raysPerProbeOption = raysPerProbeOptionIndex(state.raysPerProbe);
        if (ui->comboBox("Rays per probe", &raysPerProbeOption, {"16", "32", "64", "128", "256"})) {
            state.raysPerProbe = kRaysPerProbeOptions[static_cast<size_t>(raysPerProbeOption)];
            state.fixedRayCount = (std::min)(state.fixedRayCount, state.raysPerProbe);
        }

        int32_t fixedRayOption = raysPerProbeOptionIndex(state.fixedRayCount);
        if (ui->comboBox("Fixed rays", &fixedRayOption, {"16", "32", "64", "128", "256"})) {
            state.fixedRayCount = (std::min)(kRaysPerProbeOptions[static_cast<size_t>(fixedRayOption)], state.raysPerProbe);
        }

        int32_t phaseOption = updatePhaseOptionIndex(state.probeUpdatePhaseCount);
        if (ui->comboBox("Update phases", &phaseOption, {"1", "2", "4", "8"})) {
            state.probeUpdatePhaseCount = kUpdatePhaseOptions[static_cast<size_t>(phaseOption)];
        }

        int32_t distributionMode = static_cast<int32_t>(state.distributionMode);
        if (ui->comboBox("Probe distribution", &distributionMode, {"UniformInSceneBounds", "ManualVolume"})) {
            state.distributionMode = static_cast<ProbeDistributionMode>(distributionMode);
        }
        // These controls affect the next generated probe layout, so keep them
        // near the rest of the editable settings instead of mixing them with
        // read-only status values.
        ui->inputFloat("Volume offset X", &state.volumeOffset.x, 0.10f, 3);
        ui->inputFloat("Volume offset Y", &state.volumeOffset.y, 0.10f, 3);
        ui->inputFloat("Volume offset Z", &state.volumeOffset.z, 0.10f, 3);

        if (ui->button("Apply DDGI settings")) {
            state.requestApplyProbeLayout = true;
        }
        if (ui->button("Clear probe history")) {
            state.requestClearProbes = true;
        }

        // The bundled HUD wrapper only exposes interactive widgets. Reusing a
        // button as a static label keeps the current probe summary visible
        // without reaching into vulkan_base to add a new text API.
        const std::string probeCountLabel = std::format("Probe count: {}", state.probeCount);
        ui->button(probeCountLabel.c_str());

        if (state.showProbeStatusStats) {
            const std::string inactiveProbeLabel = std::format("Inactive probes: {}", state.inactiveProbeCount);
            ui->button(inactiveProbeLabel.c_str());

            const std::string activeProbeLabel = std::format("Active probes: {}", state.activeProbeCount);
            ui->button(activeProbeLabel.c_str());

            const std::string backfaceProbeLabel = std::format("Backface-heavy: {}", state.inactiveBackfaceCount);
            ui->button(backfaceProbeLabel.c_str());

            const std::string noGeometryProbeLabel = std::format("No geometry hit: {}", state.inactiveNoGeometryCount);
            ui->button(noGeometryProbeLabel.c_str());

            const std::string noLocalFrontfaceProbeLabel = std::format("No local frontface: {}", state.inactiveNoLocalFrontfaceCount);
            ui->button(noLocalFrontfaceProbeLabel.c_str());

            const std::string onlyBackfaceProbeLabel = std::format("Only backface: {}", state.inactiveOnlyBackfaceCount);
            ui->button(onlyBackfaceProbeLabel.c_str());
        }

        const std::string raysLabel = std::format("Rays / probe: {}", state.raysPerProbe);
        ui->button(raysLabel.c_str());

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

    }
}

} // namespace debug
