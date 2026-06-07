#pragma once
// HUD OVERLAY BINDER — wire the in-game HUD into the live frame (PLAYABLE_PLAN P2/P5).
//
// The spine's per-frame loop drives four HUD steps via ISubsystems (all already
// reach reconstructed gui code, but with DEGENERATE headless inputs):
//
//   * hudLabelsAndCaption  -> gui::Hud_LabelLayout / Hud_ButtonRowLayout
//   * hudSelectionAndTargets-> gui::StatusText_Register / DamageLabel_Register
//   * tooltipDispatch       -> gui::Tooltip_ClassifySubject / Tooltip_SelectBuilder
//
// Headless there is no live scene, so the in-frame tooltip step always classifies
// the NULL scene-reference -> TooltipKind::kNone (inert: no subject), the label
// step lays out one fixed caption, and the player-bar / button rows are never
// exercised. This binder DE-INERTS the HUD content layer: it builds a small real
// scene-object table so a hovered object actually classifies to a real builder,
// registers real status-text/damage entries, lays out the bottom player bar, and
// places label/button rows — all through the SAME reconstructed gui functions the
// live frame uses. It is additive (no edits to wiring.cpp): the e2e drives real
// PlayableApp frames (proving the inert hud* hooks reach real code) AND drives the
// binder to produce observable, non-inert HUD output.
//
// Determinism (PLAYABLE_PLAN B1): the binder mutates only the gui module globals
// (g_statusText / g_damageLabels / g_playerBarSlots) plus its own owned buffer, in
// a fixed order from a fixed input — two binds from the same inputs are identical.

#include "gui/hud.h"
#include "gui/tooltip.h"
#include "gui/playerbar.h"

#include <cstdint>
#include <vector>

namespace guild::play {

// One laid-out player-bar slot: the slot index the object was assigned plus the
// resolved screen layout the bar would draw it at.
struct HudBarSlot {
    int             slotIndex = -1;
    std::uint16_t   objId     = 0;
    gui::PlayerBarLayout layout{};
};

// What the HUD overlay looked like after a bind pass (observable real output).
struct HudOverlay {
    // Label layout: a centered caption + an N-button action row.
    gui::HudLabelLayout caption{};
    std::vector<int>    buttonRowX;     // start x of each button
    int                 buttonRowPitch = 0;

    // Status / damage tables (slot indices the real table code returned).
    std::vector<int>    statusSlots;
    int                 damageSlot = -1;

    // Player bar: one entry per owned object the bar laid out.
    std::vector<HudBarSlot> barSlots;

    // The tooltip the hovered subject classified to (real classification core).
    gui::TooltipKind    hoverKind     = gui::TooltipKind::kNone;
    int                 hoverObjCode  = 0;   // resolved object code (id fallback)
    int                 hoverTextId   = 0;   // status-text id selected for the hover
};

// HudOverlayBinder — owns a tiny reconstructed scene-object table and binds the HUD
// overlay through the reconstructed gui functions. Reusable across frames.
class HudOverlayBinder {
public:
    // Reset every gui HUD table this binder touches to its create-time state, and
    // build the owned scene-object table. `objectClasses` is the class byte of each
    // 65-byte object record (drives Tooltip_SelectBuilder's Object-vs-Upgrade pick);
    // an empty list builds a default 8-object table (class byte 32 = "object").
    void reset(const std::vector<std::uint8_t>& objectClasses = {});

    // gui::Hud_LabelLayout — place the HUD caption (default centered, anchor 320).
    gui::HudLabelLayout layoutCaption(gui::HudLabelAlign align, int anchorX, gui::i16 width);

    // gui::Hud_ButtonRowLayout — even-spacing layout of an action button row.
    // Returns the chosen pitch; fills `outX`.
    int layoutButtonRow(const std::vector<int>& widths, int windowWidth,
                        std::vector<int>& outX);

    // gui::StatusText_Register — register a status-text entry (de-duped). Returns slot.
    int registerStatus(int key, int tag);

    // gui::DamageLabel_Register — register a floating damage label. Returns slot.
    int registerDamage(int source, int amount, int now);

    // gui::PlayerBar_AssignSlot + PlayerBar_SlotLayout — assign each owned object a
    // slot in the bottom bar and resolve its layout. Returns the laid-out slots.
    std::vector<HudBarSlot> buildPlayerBar(const std::vector<std::uint16_t>& objIds);

    // Classify a hovered object by its tooltip id against the owned scene table.
    // `sceneObjectIndex` >= 0 hovers a real owned scene object (its +736 ref points
    // into the object table); < 0 falls back to the widget tooltip id. Returns the
    // builder the dispatcher would fire.
    gui::TooltipKind classifyHover(int sceneObjectIndex, int tooltipId,
                                   gui::TooltipSubject& outSubject) const;

    // Unit-level: the status-text id the HUD selects for a hovered tooltip id
    // (the id-fallback object-range map: [206,1010) -> id-206, else 0).
    static int TooltipTextId(int tooltipId);

    // Bind the full overlay for one HUD pass: caption + button row + status + damage
    // + player bar + the hovered subject. Pure function of its inputs (deterministic).
    HudOverlay bind(int hoverSceneObject, int hoverTooltipId,
                    const std::vector<std::uint16_t>& ownedObjects);

    // The TooltipTables view onto the owned scene-object buffer.
    gui::TooltipTables tables() const;

private:
    std::vector<std::uint8_t> objectTable_;  // 65-byte object records, class byte at [0]
    int objectCount_ = 0;
};

} // namespace guild::play
