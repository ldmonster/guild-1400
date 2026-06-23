#pragma once
// =============================================================================
// guild::play — NATIVE "choose history / perspective" screen.
//
//   VIBE_Menu_RunChooseHistory @0x52d684 — the screen the new-game funnel runs right after
//   the difficulty pick. Form "Menu\\CHOOSEHISTORY"; title + options are the rich-text
//   markup VIBE_Text_RenderRichString(0x16CD) == `_M0_HISTORIE` with the three mode names
//   `_M0_HISTORIE_MODUS+0..2` substituted into the `%ia[%s]` slots:
//       $[Историческая справка]   (heading)
//       %ia[Фактическое историческое описание]  -> History flag 1
//       %ia[Индивидуальная история игрока]       -> History flag 2
//       %ia[Без исторической справки]            -> History flag 0
//       %in[Назад]   (the non-selecting back member)
//
// This is the native (Vulkan/SDL) FRONT of the perspective screen (the analogue of
// sdl_charintro_screen for the difficulty screen). It renders the real localized text and
// runs the SDL frame loop, returning the picked History flag (the 1:1 id->flag mapping from
// gui/choosehistory_run). The post-pick character SPINE (player/profession/character) is the
// caller's concern (native_main_menu chains the existing char-create after this screen).
//
// Reuses the difficulty screen's render primitives (CharIntroContent / CharIntroLayout /
// RenderCharIntroFrame) since both are radio-of-N panels with the identical layout.
// =============================================================================
#include "play/sdl_charintro_screen.h"   // CharIntroContent + render primitives (reused)

#include <string>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

struct ChooseHistoryConfig {
    std::string gameDir;
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;
    int frameCapMs = 16;
    int seedMode = 0;             // dword_12335AC stored perspective mode (0/1/2)
};

struct ChooseHistoryScreenResult {
    bool confirmed = false;       // a perspective mode was picked -> proceed
    bool back      = false;       // back row / ESC / window-close -> cancel
    int  historyFlag = -1;        // History_SetActiveFlag arg of the pick (1/2/0); -1 = none
    int  framesPresented = 0;
    int  hoveredRow = -1;
    bool quitByWindow = false;
    bool usedRealText = false;
};

// Map a selectable perspective row (0/1/2) to its History flag (1/2/0) — the
// gui/choosehistory_run id0->1, id1->2, id2->0 mapping.
inline int ChooseHistory_RowToFlag(int row) { return (row == 0) ? 1 : (row == 1) ? 2 : 0; }

// Load the real `_M0_HISTORIE` markup + the three `_M0_HISTORIE_MODUS+k` mode names from
// textbin_deutsch.BIN (by NAME) and assemble the screen content (heading + prompt + the
// three mode rows + a "back" row). Returns false when the text archive/entry is absent.
bool LoadChooseHistoryContent(const std::string& gameDir, CharIntroContent& out);

// Render + run the perspective screen until a mode is picked (confirm) or back/ESC/close
// (cancel). `device` MUST be init()'d to cfg.fbW x cfg.fbH.
ChooseHistoryScreenResult RunChooseHistoryScreen(shim::IGraphicsDevice& device,
                                                 shim::IPlatform& plat,
                                                 const ChooseHistoryConfig& cfg);

} // namespace guild::play
