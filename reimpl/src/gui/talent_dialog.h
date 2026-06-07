#pragma once
// guild::gui — VIBE_TalentDialog_Show @0x546ce8.  The "special talent" training parchment.
//
//   form = "Special\Talente";  SelectWindow(form,0) -> Hud_SyncWindowColors; CenterChild.
//   SelectWindow(form, talentSlot):
//     RenderRichString(4802, 4810+talent);  RenderRichString(4822+talent);  // name + desc
//   SelectWindow(form,2):
//     a slider showing the current skill points (AddSliderToWindow range 0..100) — disabled;
//     Hud_BuildTiledRow(x, 70, points) draws the current point pips.
//   SelectWindow(form,3): a status line chosen by the situation:
//     - if a trainer handler exists -> RenderRichString(4806, trainerSkill+4810, talent+4810)
//     - else if points >= 0xFC (maxed) -> 4805
//     - else if requiredLevel > availableLevel -> 4807 (can't afford the level jump)
//     - else if hasTrainingFlag -> 4803 (with cost) ; else 4804 ; and mark `canTrain=1`.
//   modal loop (RunFrameLoop 423879):
//     1210 -> if canTrain: RequestBuildOp90(building, -level); slot-reset kind 96; done.
//     1155 -> cancel.
//
// The "level" the dialog charges is derived from the current talent points via a 6-step
// threshold table: points in [0x2A,0x54)->2, [0x54,0x7E)->3, [0x7E,0xA8)->5,
// [0xA8,0xD2)->7, >=0xD2 -> 10, else (<0x2A) -> 1; then +/- a debug adjust.
//
// We recover the form name, the per-talent text-id arithmetic (4810/4822 bases, slot index),
// the slider range / tiled-row layout, the status-line selection, the point->level threshold
// table, the command kind (op 90 / reset 28 kind 96), and the confirm/cancel wiring.

#include "gui/types.h"

namespace guild::gui {

inline constexpr int kTalClickOK     = 1210;
inline constexpr int kTalClickCancel = 1155;
inline constexpr int kTalLoopForm    = 423879;

inline constexpr const char* kFormTalent = "Special\\Talente";

inline constexpr int kTalTitleSlot   = 0; // SyncWindowColors target
inline constexpr int kTalSliderSlot  = 2;
inline constexpr int kTalStatusSlot  = 3;

// Per-talent text bases (RenderRichString).
inline constexpr int kTextTalNameHdr = 4802; // header (with name id 4810+talent)
inline constexpr int kTextTalNameBase = 4810; // talent name id base
inline constexpr int kTextTalDescBase = 4822; // talent description id base
inline constexpr int kTextTalTrainer  = 4806; // a trainer is teaching this talent
inline constexpr int kTextTalMaxed    = 4805; // points maxed out (>= 0xFC)
inline constexpr int kTextTalCantJump = 4807; // required level exceeds available
inline constexpr int kTextTalCanTrain = 4803; // can train (with cost) — sets canTrain
inline constexpr int kTextTalCanTrain2 = 4804; // can train (no debug bonus) — sets canTrain

// Slider geometry (AddSliderToWindow(x, 52, 0, 64, 100, 100, 66, win)).
inline constexpr int kTalSliderMax  = 100;
inline constexpr int kTalTiledRowY  = 70; // Hud_BuildTiledRow(x, 70, points)

// Command: RequestBuildOp90(building, -level) then QueueRequestSlotReset28(kind 96).
inline constexpr int kTalResetKind = 96;
inline constexpr int kTalPointsMax = 0xFC; // >= this => maxed

// The point->level threshold table (recovered from the >=0x2A/0x54/0x7E/0xA8/0xD2 ladder).
//   < 0x2A -> 1, [0x2A,0x54) -> 2, [0x54,0x7E) -> 3, [0x7E,0xA8) -> 5,
//   [0xA8,0xD2) -> 7, >= 0xD2 -> 10.
inline constexpr int kTalThresholds[5] = {0x2A, 0x54, 0x7E, 0xA8, 0xD2};
inline constexpr int kTalLevels[6]     = {1, 2, 3, 5, 7, 10};

struct TalentState {
    int talent = 0;        // talent index (selects 4810+talent / 4822+talent)
    int points = 0;        // current skill points (byte 0..255)
    int building = 0;      // active building handle (op-90 target)
    int availableLevel = 0; // levels the player can afford (dword_12CEAA4[...])
    bool hasTrainer = false; // a trainer handler exists for this talent
    int trainerSkill = 0;   // trainer's skill level (status line 4806)
    bool hasTrainingFlag = false; // dword_12CE919 low byte set -> 4803 path (else 4804)
    int debugAdjust = 0;    // -1 / 0 from the DebugCmd toggle (LEVEL +/-)
};

struct TalentLayout {
    const char* form = nullptr;
    int talent = 0;
    int nameTextId = 0;   // 4810 + talent
    int descTextId = 0;   // 4822 + talent
    int sliderMax = kTalSliderMax;
    int sliderValue = 0;  // = points
    int statusText = 0;   // the chosen slot-3 line
    int requiredLevel = 0; // computed from points (+ debugAdjust)
    bool canTrain = false; // the 4803/4804 path was taken
};

struct TalentCommandSink {
    virtual ~TalentCommandSink() = default;
    // Train the talent: building op 90 with -level, then a reset bracket (kind 96).
    virtual void Train(int /*building*/, int /*level*/) {}
};
void TalentDialog_SetCommandSink(TalentCommandSink* sink);

// gilde.exe 0x546ce8 — the point->level threshold lookup (the v48 ladder).
int TalentDialog_PointsToLevel(int points);

// gilde.exe 0x546ce8 (layout half) — build the parchment for a talent state.
TalentLayout TalentDialog_Build(const TalentState& s);

// gilde.exe 0x546ce8 (wiring half) — 1210 trains (when canTrain); 1155 cancels.
bool TalentDialog_Dispatch(const TalentLayout& l, const TalentState& s, int clickedId);

} // namespace guild::gui
