#pragma once
// guild::gui — in-world HUD: mouse-click hit-test routing, on-screen text labels,
// the event-panel slot table, the status-text / damage-label tables, the HUD button
// row layout, and the on-screen game clock.
//
// The HUD draws the in-world overlay (selection info, object/character/animal name
// labels, scroll arrows, edge-scroll, the action/slider button rows) on top of the
// 3D scene.  This module recovers the DATA/LAYOUT/HIT-TEST half byte-for-byte:
//
//   * VIBE_Hud_HandleMouseClick @0x4bc280 — the *hot-spot / event-slot resolution*
//     (the dword_11CB560/564 stride-3 table scan that maps a hovered widget index to
//     an HUD action slot) and the v21 flag-bit dispatch (0x800 selection / 0x100 info
//     panel / 0x8 status banner).  The bulk of the original mutates world/scene/
//     building state through the sim/render clusters; those edges are forward-declared
//     and routed through a mockable command hook.
//   * VIBE_Hud_BuildButtonRow @0x4bcdfc — even-spacing button-row layout math.
//   * VIBE_Hud_FindModeIndex @0x4bea2c — locate a frame-loop mode in the mode table.
//   * VIBE_Hud_Add{Centered,RightAligned,LeftAligned}Label @0x552778/858/8b0 — the
//     label x-placement + alignment-flag layout.
//   * VIBE_StatusText_Register @0x4bcc80 — the 50-dword-stride status-text table.
//   * VIBE_DamageLabel_RegisterEntry @0x4bad5c — the 67-dword-stride damage-label
//     table with LRU eviction.
//   * VIBE_Clock_ComputeGameTimeOfDay @0x527778 — tick-accumulator -> hh:mm:ss.
//
// Glyph blit and the 3D scene render are reused/forward-declared, not reimplemented.

#include "gui/types.h"

namespace guild::gui {

// ===========================================================================
// Hit-test: event-panel / hot-spot slot table.
// dword_11CB560 (owner ptr) / dword_11CB564 (widget index) / [+2] (window) form a
// stride-3 table of 16 HUD action slots.  HandleMouseClick scans it (and EventPanel
// uses the same table) to map a hovered widget index to its slot.
// ===========================================================================
inline constexpr int kHudSlotStride = 3;  // dwords per slot
inline constexpr int kHudSlotCount  = 16; // 48/3 slots

struct HudSlot {
    i32 owner;   // [0] dword_11CB560  owner record handle (0 = free)
    i32 widget;  // [1] dword_11CB564  child widget index  (-1 = free)
    i32 window;  // [2]                form/window handle
};
extern HudSlot g_hudSlots[kHudSlotCount];

void ResetHudSlots();

// gilde.exe 0x4bc280 (hot-spot scan, the `while` loop over dword_11CB564).
// Find the slot whose widget index equals `hoverWidget` (skipping free slots, marker
// -1).  Returns the slot index 0..15, or -1 when none matches.  This is the core
// HUD hit-test: it resolves a hovered widget to the HUD action slot it belongs to.
int Hud_FindSlotForWidget(int hoverWidget);

// gilde.exe 0x4c5460/0x4c5b40 — the first-free-slot scan used when creating an
// event/action slot: the lowest index with widget==-1, or -1 when all 16 are taken.
int Hud_FindFreeSlot();

// ---------------------------------------------------------------------------
// HandleMouseClick flag-bit dispatch (the v21 / a1 click-mode word).  The original
// branches on these bits to decide which HUD subsystem handles the click.
// ---------------------------------------------------------------------------
inline constexpr int kClickFlagSelection   = 0x800; // selection / room-change
inline constexpr int kClickFlagInfoPanel   = 0x100; // info-panel update
inline constexpr int kClickFlagStatusBanner = 0x8;  // status-banner / selection-flag

enum class HudClickAction {
    kNone,
    kSelection,    // 0x800 set: VIBE_Building_HandleSelectionClick + selection reset
    kInfoPanel,    // 0x100 set: VIBE_InfoPanel_Update
    kStatusBanner, // 0x8   set: compute selection flags + status text
};

// gilde.exe 0x4bc280 (the v21 bit test at LABEL_31/36/45).  Classify a click-mode
// word into the dominant HUD action.  Mirrors the original priority: 0x800 first
// (selection), then 0x100 (info panel), then 0x8 (status banner).  Pure dispatch
// classification — the mutation each action performs is routed through the command
// hook (see Hud_DispatchClick).
HudClickAction Hud_ClassifyClick(int clickMode);

// ---------------------------------------------------------------------------
// Command hook (mockable).  HandleMouseClick mutates building selection / info-panel
// / status state through the sim cluster; to keep this module testable those edges go
// through a single dispatch sink the test can observe.
// ---------------------------------------------------------------------------
struct HudCommandSink {
    virtual ~HudCommandSink() = default;
    virtual void OnSelection() {}
    virtual void OnInfoPanel() {}
    virtual void OnStatusBanner() {}
};
void Hud_SetCommandSink(HudCommandSink* sink);

// gilde.exe 0x4bc280 (top-level dispatch).  Given the click-mode word and the
// currently-hovered widget, resolve the HUD slot and fire the classified action on
// the command sink.  Returns the action taken.  (The original's deep world/scene
// mutation is the body of OnSelection/etc. on the real sink.)
HudClickAction Hud_DispatchClick(int clickMode, int hoverWidget);

// ===========================================================================
// HUD text labels — x-placement + alignment-flag layout.
// The label render writes the widget's width to +20 and an alignment flag to +88
// (center) or +92 (right/left).  These reproduce the *layout* (x/width/flag); the
// glyph blit is reused.
// ===========================================================================
enum class HudLabelAlign { kCentered, kRight, kLeft };

struct HudLabelLayout {
    int  x;       // resolved left x of the label
    i16  width;   // stored to widget +20
    int  flag88;  // widget +88 (1 for centered, untouched otherwise)
    int  flag92;  // widget +92 (1 right, 0 left, untouched for centered)
};

// gilde.exe 0x552778 / 0x552858 / 0x5528b0 — compute the label layout for the three
// alignments:
//   centered : x = anchorX - width/2 ; flag88 = 1
//   right    : x = anchorX - width   ; flag92 = 1
//   left     : x = anchorX           ; flag92 = 0
HudLabelLayout Hud_LabelLayout(HudLabelAlign align, int anchorX, i16 width);

// ===========================================================================
// HUD button row — even horizontal spacing of a row of N labelled buttons.
// gilde.exe 0x4bcdfc — VIBE_Hud_BuildButtonRow.
// Given each button's text-measured width (max over the row), the window width, and
// the button count, returns the per-button start x positions.  The original measures
// each label (Property_Get + 32), takes the max, and either spreads buttons evenly
// (width/(n+1) cells) or, when the widest button exceeds a cell, centres a fixed
// (maxW+8)-pitch run.  We expose the layout math; `widths[i]` are the measured label
// widths (Property_Get(text)+32).  Writes `outX[i]` (start x of button i) and returns
// the chosen pitch.
// ===========================================================================
int Hud_ButtonRowLayout(const int* widths, int count, int windowWidth, int* outX);

// gilde.exe 0x4bea2c — VIBE_Hud_FindModeIndex.
// The frame-loop "mode" table dword_11BBC30 is a stride-2 array; entry [2*i] is the
// mode's function tag (4*modeId in the original call).  Given the current top index
// `curIndex` and a target `modeId`, scan downward for the entry whose tag == 4*modeId
// and return the *depth* (curIndex - foundIndex).  Returns 4*modeId unchanged when not
// found or curIndex<0 (the original's fallback).
int Hud_FindModeIndex(int modeId, int curIndex, const int* modeTable);

// ===========================================================================
// Status-text table — gilde.exe 0x4bcc80, VIBE_StatusText_Register.
// dword_11B5220 is a 50-dword-stride table of <=32 entries.  Register de-dupes by the
// object key (StrCmp at +1), evicts nothing (errors at 32), and stores the entry's
// slot pointer at +0, a tag at +18-words (label) and key at +1.  We model the slot
// table indices and the de-dup; the actual mesh-callback handle and string copies are
// reused.  Returns the slot index 0..31, or -1 on overflow / not-found.
// ===========================================================================
inline constexpr int kStatusTextStride = 50; // dwords per entry
inline constexpr int kStatusTextCount  = 32;

struct StatusTextEntry {
    bool inUse;
    int  key;   // object id key (de-dup match)
    int  tag;   // a2 (the registered tag, +1 dword in original via dword_11B5264)
};
extern StatusTextEntry g_statusText[kStatusTextCount];

void ResetStatusText();

// gilde.exe 0x4bcc80 — register/find a status-text entry by object key.  If `key` is
// already present, marks it active and returns its slot.  Otherwise allocates the
// first free slot (errors -> -1 at 32 entries), stores key+tag.  Returns the slot
// index, or -1.
int StatusText_Register(int key, int tag);

// ===========================================================================
// Damage-label table — gilde.exe 0x4bad5c, VIBE_DamageLabel_RegisterEntry.
// dword_11B73A0 is a 67-dword-stride table of 64 floating damage labels.  Register
// de-dupes by source-object handle; when full it evicts the oldest entry (smallest
// timestamp at +1).  Each entry stores: +0 amount, +1 timestamp (now=dword_62EB38),
// +2 source handle, +12.. the text.  Returns the slot index, or -1 / pre-existing
// slot.  The text copy is reused.
// ===========================================================================
inline constexpr int kDamageLabelStride = 67; // dwords per entry
inline constexpr int kDamageLabelCount  = 64;

struct DamageLabelEntry {
    int amount;     // +0
    int timestamp;  // +1
    int source;     // +2  source object handle (de-dup / eviction key)
    bool inUse;
};
extern DamageLabelEntry g_damageLabels[kDamageLabelCount];

void ResetDamageLabels();

// gilde.exe 0x4bad5c — register a damage label for `source`, amount `amount`, at time
// `now` (dword_62EB38).  De-dupes by `source` (returns the existing slot).  When the
// table is full, evicts the entry with the smallest timestamp older than `now`.
// Returns the slot index, or -1 when no slot is available.
int DamageLabel_Register(int source, int amount, int now);

// ===========================================================================
// On-screen clock — DISPLAY DERIVATIVE of gilde.exe 0x527778.
//
// NOTE (corrected provenance): the real VIBE_Clock_ComputeGameTimeOfDay
// @0x527778 is a TimeBase proc that ADVANCES the master GameTime record
// qword_122F840 by   round-to-nearest((dword_1233558 * 0.00625 + 0.5) *
// dword_63CC60)   game seconds per fire, where dword_1233558 is the GAME
// SPEED setting (40 * level) — not a tick accumulator. The authoritative
// reconstruction lives in sim::ClockComputeGameTimeOfDay
// (src/sim/game_clock_tick.h); the HUD time-of-day caption should read the
// world clock qword_13CE852 (sim::g_tickClock). This helper keeps the same
// seconds expression as an hh:mm:ss splitter for existing HUD callers.
// ===========================================================================
inline constexpr float  kClockTickScale = 0.00625f; // flt_622958 (= 1/160)
inline constexpr double kClockTickBias  = 0.5;       // dbl_622960
extern int g_dayLengthSeconds; // dword_63CC60  (recovered value 100)

struct ClockTime { int h, m, s; int totalSeconds; };

// gilde.exe 0x527778 — convert the tick accumulator to a clock reading.
ClockTime Clock_ComputeTimeOfDay(int tickAccumulator);

} // namespace guild::gui
