// =============================================================================
// snow_update.cpp — wave-21 companion to the VIBE_Snow_UpdateFlake (0x42a644)
// reconstruction that lives in snow.cpp. This file adds NO redefinition of the
// integrator (reused via the snow.h declaration); it only provides the
// deterministic golden driver used by the trajectory pin tests. namespace
// guild::render.
// =============================================================================
#include "render/snow_update.h"

namespace guild::render {

void SnowGoldenStep(SnowSystem& sys, int steps, float dt, const SnowCamera& cam,
                    const SnowViewport& vp) {
    SnowSeedFlakes(sys);                 // 0x42a014 seed path (six RandNext/flake)
    for (int i = 0; i < steps; ++i)
        SnowUpdateFlake(sys, dt, cam, vp); // 0x42a644 per-frame integrate+project
}

} // namespace guild::render
