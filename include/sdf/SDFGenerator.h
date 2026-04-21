#pragma once

#include "scene/Scene.h"
#include "sdf/SDFVolume.h"

namespace sdf {

class SDFGenerator {
public:
    void generateFromScene(const scene::Scene& scene, SDFVolume& outputVolume);
};

} // namespace sdf
