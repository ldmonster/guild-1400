#pragma once
// =============================================================================
// guild::play — the native CHOOSECITY screen (the real new-game city pick).
//
// Faithful native (Vulkan/SDL) front for VIBE_Menu_RunChooseCity @0x52e6d8
// (form "Menu\CHOOSECITY" + "Menu\CHOOSECITY_HEADER").  The original is a 3D
// scene: it enumerates the ".CTY"/".NET" save files in "gamedata/cities"
// (VIBE_SaveBrowser_EnumerateSaveFiles), spawns one clickable city-point marker
// per city (VIBE_Map_SpawnCityPointMarker), registers each city's name as
// "stadt_<file>" + an info/description string ("_STADTAUSWAHL_<name>_INFO+0" /
// "..._BESCHR+0"), then per frame:
//   - picks the nearest object under the cursor (VIBE_Pick_FindNearestObjectAt);
//     a hovered object whose name starts with "stadt_" is a city marker
//     (ChooseCity_IsCityObject @0x52ea33) -> shows its description (rich text)
//     and highlights it ($C / wimpel flag);
//   - confirms on dword_75BF38 == 1210 (the confirm button) or key 28
//     (ChooseCity_IsConfirm @0x52ed4e) -> chains ChooseCharacterIntro/History;
//   - the cancel path leaves v77/v80 == 0 (no city) and returns.
//
// This module renders the equivalent 2D screen with the REAL menu artwork
// (gfx _MENUE_BACKGROUND #1773 via play::MenuAssets) plus a framed list of the
// available cities (real names via render::DrawText), a hover highlight, and a
// Confirm + Back control.  Left-click a city + Confirm (or double-click a city)
// returns it; ESC / Back returns {back=true}.  Backend-agnostic: testable
// headless (MemoryGraphicsDevice + a sequenced IPlatform), drives real Vulkan+
// SDL unchanged.  No Wine.  Additive — does NOT touch sdl_menu.cpp/menu_assets.
//
// Shared screen contract (mirrors play::RunSdlMenu):
//   CityScreenResult RunCityScreen(IGraphicsDevice&, IPlatform&, CityScreenConfig)
// =============================================================================
#include <string>
#include <utility>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

struct CityScreenConfig {
    std::string gameDir;          // mounted game dir (for the real gilde.gfx bg)
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;          // -1 = until confirm/back/close; tests bound it
    int frameCapMs = 16;          // per-frame sleep (~60fps); 0 = uncapped
    // The selectable cities: {displayName, ctyPath relative to the game dir},
    // mirroring SdlMenuConfig.cities.  Empty -> a "no cities" screen that only
    // accepts Back.
    std::vector<std::pair<std::string, std::string>> cities;
};

struct CityScreenResult {
    bool confirmed = false;       // a city was picked + confirmed
    bool back      = false;       // ESC / Back / window-close (no city)
    int  cityIndex = -1;          // index into cfg.cities of the chosen city
    std::string cityPath;         // ctyPath of the chosen city
    std::string cityName;         // displayName of the chosen city
    int  hoveredItem = -1;        // last hovered city row (-1 = none)
    int  framesPresented = 0;
    bool backByWindow = false;    // window close
    bool backByEsc = false;       // ESC key
    bool sawBackground = false;   // the real _MENUE_BACKGROUND was drawn
};

// Render + run the CHOOSECITY screen until the user confirms a city, goes Back
// (ESC / Back button / window close).  `device` MUST be init()'d to
// cfg.fbW x cfg.fbH (any bpp; composited 32bpp then converted).  `plat`'s
// window MUST already exist.
CityScreenResult RunCityScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const CityScreenConfig& cfg);

} // namespace guild::play
