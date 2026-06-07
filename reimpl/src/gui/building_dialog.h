#pragma once
// guild::gui — the building transaction dialogs (sell / renovate / expand / upgrade /
// extinguish-fire / tech-effects).
//
// Each VIBE_BuildingDialog_* function (gilde.exe 0x54a734..0x54d5b4) is a modal
// confirm dialog over a building:
//   1. form  = VIBE_GameTick_Finalize(0, 0, "<form name>");   // load the .form
//   2. VIBE_Form_CenterChildWindows(form);
//   3. VIBE_Form_SelectWindow(form, slot); VIBE_Text_RenderRichString(textId, ...);
//      VIBE_Form_GetChildObjectId(...) -> the dialog's clickable object ids;
//      (price / quantity content is computed from the building and shown via
//       VIBE_Object_SetValueOrText);
//   4. while (VIBE_GameLogic_RunFrameLoop(loopForm, ...)) {            // modal loop
//        if (dword_672230) cancel;                                     // right-click
//        switch (dword_75BF38 / dword_62D22C) {                        // which button
//          case OK(1210): enqueue the building command;  done;
//          case Cancel(1155): done;
//        }
//      }
//   5. VIBE_Form_Destroy(form);
//
// This module recovers, byte-for-byte:
//   - the recovered .form resource name for each dialog;
//   - the window-slot / text-id / child-object-id layout each dialog builds;
//   - the button -> action wiring (which clicked id enqueues which command, with the
//     command opcode constants);
//   - the price/worth content fed to the dialog from a synthetic building state.
//
// The frame loop, glyph/text engine, coordinate transforms and command codec live in
// other clusters; they are forward-declared and routed through a mockable command sink,
// so the layout + wiring + content is testable in isolation. The well-known click ids
// are the GUI-wide constants 1210 (OK / confirm) and 1155 (Cancel / right button).

#include "gui/types.h"

namespace guild::gui {

// GUI-wide last-clicked-widget ids (dword_75BF38). Shared with the dialog cluster.
inline constexpr int kClickOK     = 1210;  // confirm / OK
inline constexpr int kClickCancel = 1155;  // cancel / right button

// ---------------------------------------------------------------------------
// Recovered .form resource names (the VIBE_GameTick_Finalize string argument).
// ---------------------------------------------------------------------------
inline constexpr const char* kFormSellPergament = "Panel\\Gebaeude_Verkaufen_pergament"; // SellPreview / ConfirmSell / SellLand
inline constexpr const char* kFormRenovate      = "Panel\\Gebaeude_Renovieren";          // Renovate
inline constexpr const char* kFormExpandRoom    = "Panel\\Gebaeude_Raum_Erweitern";      // ExpandRoom
inline constexpr const char* kFormExtinguish    = "Panel\\Gebaeude_Brand_loeschen";      // ExtinguishFire
inline constexpr const char* kFormTechEffects   = "techtree\\auswirkungen";              // ShowTechEffects

// Recovered command-action label strings (VIBE_Command_EnqueueBuildingActionStart arg).
inline constexpr const char* kActionRenovate    = "renovieren";    // Renovate
inline constexpr const char* kActionRoomUpgrade = "room_upgrade";  // ExpandRoom

// Recovered RunFrameLoop "form" arguments (the modal-loop selector).
inline constexpr int kLoopFormConfirm = 423879;  // single-confirm dialogs
inline constexpr int kLoopFormPanel   = 415687;  // panel-style dialogs (Expand/Options)

// Recovered text-string ids passed to VIBE_Text_RenderRichString per dialog.
inline constexpr int kTextSellPreview   = 5080;   // SellPreview body
inline constexpr int kTextRenovate      = 5083;   // Renovate body (4 child objects)
inline constexpr int kTextConfirmSell   = 5087;   // ConfirmSell body
inline constexpr int kTextExpandTitle   = 5089;   // ExpandRoom title
inline constexpr int kTextSellLand      = 5105;   // SellLand body
inline constexpr int kTextTechTitle     = 5107;   // TechEffects title
inline constexpr int kTextTechBody      = 5108;   // TechEffects body
inline constexpr int kTextFireTitle     = 5111;   // ExtinguishFire title
inline constexpr int kTextFireBody      = 5112;   // ExtinguishFire body (3 buttons)

// Recovered command opcodes (the byte/word the command packets carry).
inline constexpr int kCmdSell        = 90;  // SellPreview/ConfirmSell/SellLand op 90
inline constexpr int kCmdReset28     = 27;  // QueueRequestSlotReset28 kind byte (Renovate=28)
inline constexpr int kCmdSellLandTag = 33;  // SellLand reset tag

// The tech-effects category order table (gilde.exe dword_53B454), byte-exact.
// 17 active category ids terminated by a 0; ShowTechEffects walks it to build a slider
// per category that has a workstation.
inline constexpr int kTechCategoryCount = 17;
extern const u8 kTechCategoryOrder[kTechCategoryCount + 1]; // trailing 0 sentinel

// ---------------------------------------------------------------------------
// Synthetic building state — the BuildingValue source the dialogs read.
//
// The originals compute a sell/renovate/expand worth from the building record via
// VIBE_BuildingValue_ComputeRoomWorth + VIBE_Money_ConvertToDisplayCoord, and gate the
// dialog on requirements (VIBE_Building_CheckBuildRequirements, condition byte at
// building+89>>24). A synthetic state carries the inputs those functions consume.
// ---------------------------------------------------------------------------
struct BuildingState {
    int handle = 0;       // building handle (building+1 == the entity id)
    int roomWorth = 0;    // VIBE_BuildingValue_ComputeRoomWorth result
    int condition = 100;  // *(building+89) >> 24  (100 = pristine; <100 => renovatable)
    int buildType = 0;    // *(building) building type byte
    bool meetsRequirements = true; // VIBE_Building_CheckBuildRequirements == 1
};

// The widget set a dialog builds (the ids it gets back from Form_GetChildObjectId).
struct DialogLayout {
    const char* form = nullptr; // resolved .form name
    int textId = 0;             // primary RenderRichString id
    int childCount = 0;         // number of child objects fetched
    int childIds[8]{};          // the fetched child-object ids (objBase, objBase+1, ...)
    int displayWorth = 0;       // the price/worth shown in the dialog
    int loopForm = 0;           // RunFrameLoop selector
};

// ---------------------------------------------------------------------------
// Command sink (mockable) — the building commands a confirmed dialog enqueues.
// Each method names the original VIBE_Command_* it forwards to.
// ---------------------------------------------------------------------------
struct BuildingCommandSink {
    virtual ~BuildingCommandSink() = default;
    // SellPreview / SellLand / ConfirmSell: queue a sell (op 90) for the building.
    virtual void Sell(int /*building*/, int /*op*/, int /*price*/) {}
    // Renovate: building-action start/cmd15/end bracket (label = "renovieren").
    virtual void Renovate(int /*building*/, int /*cost*/, const char* /*label*/) {}
    // ExpandRoom: room upgrade (label = "room_upgrade").
    virtual void ExpandRoom(int /*building*/, int /*itemId*/, int /*cost*/, const char* /*label*/) {}
    // ExtinguishFire: building op 90 with the chosen effort level (1/2/4).
    virtual void ExtinguishFire(int /*building*/, int /*level*/) {}
    // TechEffects: read-only (shows a sub-messagebox); no command.
    // Upgrade (0x54b604): EnqueueBuildingActionStart("building_upgrade") +
    // QueueRequestSlotReset28(kind=30) bracket; `cost` is the displayed price.
    virtual void Upgrade(int /*building*/, int /*cost*/, const char* /*label*/) {}
};
void BuildingDialog_SetCommandSink(BuildingCommandSink* sink);
// The currently-installed sink (never null; defaults to an inert sink). Shared with
// the upgrade/expand and options-menu dialogs so the hub can dispatch through it.
BuildingCommandSink* BuildingDialog_CommandSink();

// ---------------------------------------------------------------------------
// Layout builders (the DATA/LAYOUT half) — return the widget set each dialog builds for
// a synthetic building, without entering the modal loop.
// ---------------------------------------------------------------------------

// gilde.exe 0x54a734 — VIBE_BuildingDialog_SellPreview.
// Form = sell-pergament, window slot 2, text 5080, 1 child (the price object); worth =
// the room worth converted to a display coordinate.
DialogLayout BuildingDialog_BuildSellPreview(const BuildingState& b);

// gilde.exe 0x54ad7c — VIBE_BuildingDialog_ConfirmSell.
// Form = sell-pergament, text 5087, 1 child. The sell is dispatched (op 90) when the
// child object is clicked with id 1210.
DialogLayout BuildingDialog_BuildConfirmSell(const BuildingState& b);

// gilde.exe 0x54bc50 — VIBE_BuildingDialog_SellLand.
// Gated on meetsRequirements (else messagebox 0). Form = sell-pergament, slot 2, text
// 5105, 1 child; worth = converted room worth. Sell op 90 + reset tag 33 on confirm.
DialogLayout BuildingDialog_BuildSellLand(const BuildingState& b);

// gilde.exe 0x54a908 — VIBE_BuildingDialog_Renovate.
// Gated on (100 - condition) > 0 (else messagebox 4). Form = renovate, text 5083, 4
// child objects (3 cost variants + cancel). Confirm enqueues Renovate("renovieren").
DialogLayout BuildingDialog_BuildRenovate(const BuildingState& b);

// gilde.exe 0x54c3c0 — VIBE_BuildingDialog_ExtinguishFire.
// Form = extinguish, slot 1 (title 5111) + slot 2 (body 5112), 3 effort buttons
// (level 1/2/4). Confirm dispatches ExtinguishFire(level) after a skill check.
DialogLayout BuildingDialog_BuildExtinguishFire(const BuildingState& b);

// gilde.exe 0x54be50 — VIBE_BuildingDialog_ShowTechEffects.
// Form = tech-effects, slot 1 (title 5107) + slot 3 (body 5108 + OK 1210) + slot 2 (one
// slider per active tech category that has a workstation). Read-only. childCount = the
// number of category sliders built for the synthetic state's workstation set.
DialogLayout BuildingDialog_BuildTechEffects(const BuildingState& b,
                                             const bool* hasWorkstation /*[17]*/);

// ---------------------------------------------------------------------------
// Wiring (the WIRING half) — map a clicked id to the dialog's action via the sink.
// `layout` is the widget set returned by the matching Build*; `clickedId` is the live
// dword_75BF38 (1210/1155) and `clickedObj` is dword_62D22C (which child was hit).
// Returns true when the click confirmed/dispatched (the loop should end).
// ---------------------------------------------------------------------------
bool BuildingDialog_DispatchSellPreview(const DialogLayout& l, const BuildingState& b,
                                        int clickedId, int clickedObj);
bool BuildingDialog_DispatchConfirmSell(const DialogLayout& l, const BuildingState& b,
                                        int clickedId, int clickedObj);
bool BuildingDialog_DispatchSellLand(const DialogLayout& l, const BuildingState& b,
                                     int clickedId, int clickedObj);
bool BuildingDialog_DispatchRenovate(const DialogLayout& l, const BuildingState& b,
                                     int clickedId, int clickedObj);
// ExtinguishFire: clickedObj selects effort level (childIds[0]=lvl1, [1]=lvl2, [2]=lvl4).
bool BuildingDialog_DispatchExtinguishFire(const DialogLayout& l, const BuildingState& b,
                                           int clickedId, int clickedObj);

} // namespace guild::gui
