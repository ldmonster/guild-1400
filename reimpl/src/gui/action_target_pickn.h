#pragma once
// guild::gui — action_target_pickn: a P6 (Wave 29) coverage slice closing the deferred
// VIBE_ActionDialog_* target-pick builders, translated 1:1 from gilde.exe. Verified
// absent from src/ + include/ (address AND VIBE_ name) before work.
//
//   0x548c0c  VIBE_ActionDialog_BeginAbductTargetPick  build the abduct target-pick
//                                                       config record {flag=1024,
//                                                       kind=6} + grey overlay, hand it
//                                                       to the office-overview window
//                                                       with the ConfirmAbduct callback.
//   0x5488ac  VIBE_ActionDialog_PromptTargetSelect     format a prompt message (text id
//                                                       4962), build the same target-pick
//                                                       config record, open the window,
//                                                       then kick the edge-scroll updater.
//
// Both pack a small target-pick config record and call the office-overview window +
// (for PromptTargetSelect) Hud_UpdateEdgeScroll; those runtime leaves are routed
// through an installable ActionTargetPickHooks struct with inert defaults. The
// GUI-OWNED deterministic core is the config-record layout, exposed as a pure helper
// so it is golden-vector testable.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Target-pick config record. Recovered from the v3[]/v6[] stack layouts:
//   +0 (dword)  grey-overlay color descriptor   (Light_SetGrayColorThunk(0,40,&rec))
//   +4 (dword)  flag / selection mask  = 1024
//   +8 (dword)  payload (this / context)
//   +12 (byte)  kind = 6   (the pick-mode tag)
// PromptTargetSelect uses the same record but additionally sets +8 (payload) = 0 and a
// kind byte of 6.
// ---------------------------------------------------------------------------
struct TargetPickConfig {
    std::int32_t overlayColor; // +0
    std::int32_t flag;         // +4  (= 1024)
    std::int32_t payload;      // +8
    std::uint8_t kind;         // +12 (= 6)
};

// The abduct office-table pointer (original dword_8C845C); settable for tests.
extern const void* g_abductOfficeTable;

inline constexpr std::int32_t kTargetPickFlag = 1024;
inline constexpr std::uint8_t kTargetPickKind = 6;
inline constexpr int kAbductGrayLevel  = 40;    // Light_SetGrayColorThunk(0, 40, ...)
inline constexpr int kPromptTextId     = 4962;  // Text_RenderFormattedMessage id

// gilde.exe 0x548c20.. / 0x5488c3.. — pure config-record builder.
// Packs {overlayColor=grayLevel, flag=1024, payload, kind=6}.
TargetPickConfig ActionDialog_BuildTargetPickConfig(int grayLevel, std::int32_t payload);

// ===========================================================================
// Hooks: office-window + text + edge-scroll leaves with inert defaults.
// ===========================================================================
struct ActionTargetPickHooks {
    void (*lightSetGrayThunk)(int a, int level, std::int32_t* recOut); // VIBE_Light_SetGrayColorThunk @0x5c6af0
    int  (*amtRunOfficeOverviewWindow)(const TargetPickConfig* cfg, const void* textOrTable, const void* callback); // VIBE_Amt_RunOfficeOverviewWindow @0x5575c8
    void (*textRenderFormattedMessage)(char* buf, int textId); // VIBE_Text_RenderFormattedMessage @0x59f99c
    void (*hudUpdateEdgeScroll)(int a, int b, int c);          // VIBE_Hud_UpdateEdgeScroll @0x4bc07c
    int  (*confirmAbductCallback)();                           // VIBE_ActionDialog_ConfirmAbduct @0x54891c
};

const ActionTargetPickHooks* SetActionTargetPickHooks(const ActionTargetPickHooks* hooks);
const ActionTargetPickHooks* ActionTargetPickHooks_Default();
const ActionTargetPickHooks& ActionTargetPickHooksActive();
void ResetActionTargetPick();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// gilde.exe 0x548c0c — VIBE_ActionDialog_BeginAbductTargetPick (this@ecx).
// Builds the abduct config record (grey level 40, flag 1024, kind 6, payload=this),
// then opens the office-overview window pointing at the abduct table (dword_8C845C)
// with the ConfirmAbduct callback. Returns the window result.
int ActionDialog_BeginAbductTargetPick(std::int32_t self);

// gilde.exe 0x5488ac — VIBE_ActionDialog_PromptTargetSelect (this@ecx, a2@edx, a3@edi,
// a4@esi). Formats the prompt message (text id 4962) into a scratch buffer, builds the
// target-pick config record (grey 40, flag 1024, payload 0, kind 6), opens the
// office-overview window with that message, then triggers Hud_UpdateEdgeScroll(0,a3,a4).
void ActionDialog_PromptTargetSelect(std::int32_t self, int a2, int a3, int a4);

} // namespace guild::gui
