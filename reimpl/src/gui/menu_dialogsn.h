#pragma once
// guild::gui — menu_dialogsn: a P6 (Wave 29) coverage slice closing genuinely-DEFERRED
// deterministic leaves of the VIBE_Menu_* family, translated 1:1 from gilde.exe.
// Verified absent from src/ + include/ (address AND VIBE_ name) before work.
//
//   0x528c84  VIBE_Menu_ChoosePlayerCount   the player-count picker modal: clamp the
//                                           count to >=2, run the picker loop, return
//                                           whether OK was pressed.
//   0x52c6c0  VIBE_Menu_ShowMissionWarning  pick a random mission by type, optionally
//                                           show a warning dialog (gated on >1 player),
//                                           then run the failure dialog if a mission hit.
//
// Each is dominated by the retained-mode form runtime (GameTick_Finalize, the frame
// loop, Text_RenderRichString) which is routed through an installable MenuDialogsNHooks
// struct with inert defaults in menu_dialogsn.cpp. The GUI-OWNED deterministic cores —
// the player-count clamp and the >1-player warning gate + result mapping — are pure,
// golden-vector testable helpers.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Menu state (originals: byte_63CC1D player-count, dword_75BF38 last-action,
// dword_672230 mouse-release, byte_63C8F4 mission-type seed).
// ---------------------------------------------------------------------------
extern std::uint8_t g_playerCount;   // byte_63CC1D
extern int g_lastMenuAction;         // dword_75BF38
extern std::uint8_t g_missionTypeSeed; // byte_63C8F4

// The last-action codes the dialog loops react to (verbatim constants).
inline constexpr int kActionCancel = 1155;  // -> latch quit, OK not pressed
inline constexpr int kActionOk     = 1210;  // -> commit selection / OK pressed

// ChoosePlayerCount clamps the stored player count to a minimum of 2.
inline constexpr std::uint8_t kMinPlayerCount = 2;

// gilde.exe 0x528cb9 region — player-count clamp: max(current, 2).
std::uint8_t Menu_ClampPlayerCount(std::uint8_t current);

// gilde.exe 0x52c6e7 region — ShowMissionWarning only shows the warning dialog when
// more than one player is configured (byte_63CC1D > 1).
bool Menu_ShouldShowMissionWarning(std::uint8_t playerCount);

// ===========================================================================
// Hooks: form/text runtime leaves with inert defaults.
// ===========================================================================
struct MenuDialogsNHooks {
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName); // VIBE_GameTick_Finalize @0x41beb8
    int  (*formCenterChildWindows)(int formId);                 // VIBE_Form_CenterChildWindows @0x41d6ac
    int  (*formGetChildObjectId)(int form, int group, int child); // VIBE_Form_GetChildObjectId @0x41dea8
    void (*objectSetValueOrText)(int widget, int a, int b, int c);// VIBE_Object_SetValueOrText @0x41dfec
    void (*dragCursorSetSprite)(int a, int b);                  // VIBE_DragCursor_SetSprite @0x41fcbc
    int  (*formSelectWindow)(int formId, int winSlot);          // VIBE_Form_SelectWindow @0x41e4cc
    int  (*textRenderRichString)(unsigned a, unsigned b);       // VIBE_Text_RenderRichString @0x59d6e8
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* p); // VIBE_GameLogic_RunFrameLoop @0x4c09a0
    int  (*objectGetDataPtr)(int widget);                       // VIBE_Object_GetDataPtr @0x41db9c
    void (*formSetObjectsVisible)(int formId, int hide);        // VIBE_Form_SetObjectsVisible @0x41d634
    void (*formDestroy)(int formId);                            // VIBE_Form_Destroy @0x41da04
    int  (*readMouseRelease)();                                 // dword_672230
    // mission leaves
    int  (*missionPickRandomByType)(std::uint8_t seed);         // VIBE_Mission_PickRandomByType @0x538680
    void (*missionRunFailureDialog)(int mission, int a2);       // VIBE_Mission_RunFailureDialog @0x539e8c
};

const MenuDialogsNHooks* SetMenuDialogsNHooks(const MenuDialogsNHooks* hooks);
const MenuDialogsNHooks* MenuDialogsNHooks_Default();
const MenuDialogsNHooks& MenuDialogsNHooksActive();
void ResetMenuDialogsN();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// gilde.exe 0x528c84 — VIBE_Menu_ChoosePlayerCount.
// Opens the "Menu\CHOOSE_PLAYERCOUNT" form, clamps g_playerCount to >=2, seeds the
// spinner widget, then runs the modal loop. Sets OK when action==1210 (reads the new
// count from the widget), latches quit on action==1155 or mouse-release. Destroys the
// form. Returns 1 if OK was pressed, else 0.
int Menu_ChoosePlayerCount();

// gilde.exe 0x52c6c0 — VIBE_Menu_ShowMissionWarning.
// Picks a random mission by g_missionTypeSeed; when g_playerCount>1 shows the
// "Menu\GET_MISSION_WARNING" modal first. If a mission was picked, runs the failure
// dialog and returns its result; otherwise returns 0.
int Menu_ShowMissionWarning();

} // namespace guild::gui
