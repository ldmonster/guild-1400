#pragma once
// =============================================================================
// guild::play — the native 3D CHOOSECITY screen (the REAL new-game city pick).
//
// Where sdl_city_screen.* renders a 2D list equivalent, this renders the actual
// 3D scene the original shows: Menu/ChooseCity.ed3 (the guild Secretariat with
// the map table) composited through the engine projection (play::scene_view +
// the engine-exact render::d3_projection), a city tower (sp_STADTTURM) placed at
// each city's dummy marker, and the cursor pick resolved by play::PickNearestObject
// — the reconstruction of VIBE_Pick_FindNearestObjectAt @0x5b5a38. Hovering a
// tower highlights it + shows the city name; clicking it selects (double-click /
// Enter confirms); ESC / right-click / window-close cancels.
//
// It reuses CityScreenConfig / CityScreenResult from sdl_city_screen.h so it is a
// drop-in for RunCityScreen in the native New-Game funnel. If the 3D scene assets
// are absent (no scenes.BIN), it transparently falls back to the 2D RunCityScreen
// so the asset-less / headless build still works.
// =============================================================================
#include "play/sdl_city_screen.h"   // CityScreenConfig / CityScreenResult

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

// Render + run the 3D CHOOSECITY scene. `device` must be init()'d to
// cfg.fbW x cfg.fbH; `plat`'s window must exist. Falls back to the 2D
// RunCityScreen when the .ed3 scene cannot be loaded.
CityScreenResult RunCityScreen3D(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                 const CityScreenConfig& cfg);

} // namespace guild::play
