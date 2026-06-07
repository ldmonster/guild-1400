#pragma once
// =============================================================================
// guild::play — NATIVE CHARACTER-CREATION SCREEN(S).
//
// After ChooseCity the original runs the player/character setup spine:
//   VIBE_Menu_RunChoosePlayer    @0x52ccd8  (name + wappen + gender + faith)
//   VIBE_Menu_RunChooseHistory   @0x52d684  (difficulty)
//   VIBE_Menu_ChooseProfession   @0x52c50c  (the 8-profession grid)
//   VIBE_Menu_RunChooseCharacter @0x52bcd4  (dynasty 3D scene) + talent/intro
//
// This module is the native (Vulkan/SDL) front for the two screens that are
// worth rendering as real sprite grids:
//
//   PROFESSION  (0x52c50c)  — an 8-button grid laid out  x = 96*(i%3)+100,
//      y = 80*(i/3)+100, gfx = beruf[i] + 1349.  The real profession byte
//      table (dword_527604) is {1,2,3,4,5,6,7,11} so the sprite gfx ids are
//      {1350,1351,1352,1353,1354,1355,1356,1360}.  Click i -> profession byte.
//   WAPPEN      (0x52ccd8)  — the 8 coat-of-arms buttons, gfx = 1342 + i.
//
// It drives the reconstructed model in src/gui/newgame_setup.* (NewGameParams,
// Profession_ButtonX/Y/Gfx, Wappen_ButtonGfx, NewGame_ApplyProfession/Player)
// and produces a filled gui::NewGameParams + a start decision.
//
// Backend-agnostic, mirroring play::RunSdlMenu's contract: per-frame render into
// a 32bpp scratch -> BlitToDevice -> dev.present(); read plat.getMouse()/keyDown;
// left-click EDGE drives selection; ESC = back/cancel; cfg.maxFrames bounds it
// for headless tests; cfg.frameCapMs sleeps.  When gilde.gfx is present the grid
// cells show the REAL decoded profession/wappen sprites; absent, labelled rects.
//
// INERT / faithfully-defaulted (NOT rendered natively, by design — see .cpp):
//   - gender / faith are defaulted (gender 0 male, faith 0); RunChoosePlayer's
//     name/family text entry is defaulted to cfg.defaultName / cfg.defaultFamily.
//   - the dynasty 3D ancestry scene (0x52bcd4), the talent up/down picker
//     (0x52b088), the portrait/model cycle (0x52b6b8) and the intro variant
//     radios (0x52e4e0) are not drawn here; their params are left at the model's
//     faithful defaults.  The deterministic SELECTION/COMMIT logic for those
//     lives (and is tested) in src/gui/charcreate.*.
// =============================================================================
#include "gui/newgame_setup.h"   // gui::NewGameParams (the produced struct)

#include <string>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

struct CharCreateConfig {
    std::string gameDir;          // mounted game dir (for gfx/gilde.gfx); empty = no assets
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;          // -1 = until confirm/back/close (tests bound it)
    int frameCapMs = 16;          // per-frame sleep; 0 = uncapped

    // Faithful defaults for the parts not rendered natively (RunChoosePlayer text
    // entry + gender/faith).  These flow straight into the produced NewGameParams.
    std::string defaultName   = "Spieler";  // [Network] Name fallback (Vorname)
    std::string defaultFamily = "";         // [Network] Familienname (Nachname)
    int defaultGender = 0;                   // byte_122F4A8 (0 male, 1 female)
    int defaultFaith  = 0;                   // byte_122F4A9 (0/1)

    // Carry-in params from the upstream ChooseCity step (city/network/history).
    // Copied into the result so the produced struct is the full new-game block.
    gui::NewGameParams in;
};

struct CharCreateResult {
    bool confirmed = false;       // Confirm pressed with a profession picked -> start
    bool back      = false;       // ESC / Back pressed (cancel to the city screen)
    gui::NewGameParams params;    // the filled new-game parameter block
    int  framesPresented = 0;
    int  hoveredProfession = -1;  // last hovered profession cell (0..7), diagnostics
    int  hoveredWappen     = -1;  // last hovered wappen cell (0..7)
    bool quitByWindow = false;    // window closed mid-flow
    bool usedRealSprites = false; // true iff gilde.gfx loaded and grids drew sprites
    int  phaseReached = 0;        // 0 = profession only, 1 = reached wappen phase
};

// Render + run the native character-creation flow: PROFESSION grid -> WAPPEN
// grid -> Confirm.  ESC backs out of wappen to profession, and out of profession
// to the caller (result.back).  Returns the filled params + the start decision.
CharCreateResult RunCharCreateScreen(shim::IGraphicsDevice& device,
                                     shim::IPlatform& plat,
                                     const CharCreateConfig& cfg);

} // namespace guild::play
