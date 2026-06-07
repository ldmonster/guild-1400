#pragma once
// Building entry-permission gate for the Guild simulation (gilde.exe).
// MODULE: buildings (namespace guild::sim). File stem: building6.
//
// This file recovers VIBE_Building_CheckEntryAllowed (0x51dcd4) — the last
// untranslated function in the VIBE_Building_* prefix (the rest of the prefix is
// already reconstructed across building.cpp / building2..5 / building_type /
// building_value / building_stock / building_production / building_lifecycle /
// building_create*; see the implementer report).
//
// CheckEntryAllowed decides whether the active player may enter a building. The
// DECISION logic (the deterministic gate of early-returns) is recovered 1:1; the
// presentation tail (the "closed, come back at HH:MM" message box) is the
// original's UI side effect, routed through an installable hooks struct with an
// INERT default so the rules core is exercisable without the GUI. The time-window
// check is delegated to the REAL sibling Building3_CheckTimeWindowOpen, and the
// kind classifier to the REAL Building_MapKindToCategory.
//
// Translated function (gilde.exe address -> this file's symbol):
//   0x51dcd4 VIBE_Building_CheckEntryAllowed -> Building_CheckEntryAllowed

#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Building record fields read by the entry gate (raw byte offsets; the original
// addresses the building through a char*).
//   +0    (i8)   building TYPE code (indexes the 589-byte type table)
//   +39   (u16)  owner person index (compared against the active player)
//   +91   (i8)   flag byte: bit 2 (0x4) == "entry forbidden"; the +91 dword is
//                also the linked-good handle in other functions, but the gate
//                only reads the low flag byte here.
// ===========================================================================

// ===========================================================================
// Installable hooks: the cross-module leaves the gate calls that are NOT pure
// table reads (the selection-flag query and the UI message box). Defaults are
// INERT and defined in building6.cpp. Tests install their own.
// ===========================================================================
struct Building6Hooks {
    virtual ~Building6Hooks() = default;

    // gilde.exe dword_13CE294 — base of the 589-byte building-type-def table,
    // indexed by the building's +0 type code. The gate reads the type record's
    // +0 KIND byte (v2). Default: nullptr (table unloaded -> kind 0). Tests
    // supply a real table.
    virtual const std::uint8_t* TypeDef(unsigned typeIndex) {
        (void)typeIndex; return nullptr;
    }

    // gilde.exe 0x588dec — VIBE_Building_ComputeSelectionFlags(activePlayer,
    // building, 0, 0): the big selection/eligibility bitfield. The gate reads
    // bit 0 (0x1, "selectable") and bit 9 (0x200, "force allow"). NOT yet
    // reconstructed -> hooked. Inert default: 0 (nothing selectable).
    virtual std::int16_t ComputeSelectionFlags(unsigned activePlayer,
                                               const std::uint8_t* building) {
        (void)activePlayer; (void)building; return 0;
    }

    // gilde.exe byte_12CEAC1[536*activePlayer] — the active player's record byte
    // at +0x1B1 (a "VIP / owner-of-this-kind" flag the gate consults for kinds
    // 6/4). Default: 0. Tests set it.
    virtual std::uint8_t ActivePlayerVipByte(unsigned activePlayer) {
        (void)activePlayer; return 0;
    }

    // gilde.exe 0x59f99c / 0x4ad9dc — VIBE_Text_RenderFormattedMessage +
    // VIBE_Dialog_ShowMessageBoxGreen: the "building is closed" notice. Pure UI
    // side effect; the gate's return value does not depend on it. Inert default:
    // record nothing. `msgId` is the string-table id the original selects.
    virtual void ShowClosedMessage(int msgId, const std::uint8_t* building,
                                   int openHour, int closeHour) {
        (void)msgId; (void)building; (void)openHour; (void)closeHour;
    }
};

void           SetBuilding6Hooks(Building6Hooks* hooks);
Building6Hooks* Building6HooksGet();

// Seeds the active-player index global (word_63CC5C) and the global "free entry"
// flag (word_63C740 bit 7 / 0x80) consulted by the gate.
void SetEntryGateContext(int activePlayerIndex, bool freeEntryEverywhere);

// gilde.exe 0x51dcd4 — VIBE_Building_CheckEntryAllowed.
// Returns 1 if the active player may enter `building`, 0 otherwise. The order of
// the original's checks is preserved exactly:
//   1. +91 & 0x4 set                                  -> 0 (forbidden)
//   2. global free-entry (word_63C740 & 0x80)          -> 1
//      OR active-player VIP byte AND kind in {6,4}      -> 1
//      OR selection flag bit 0x200                      -> 1
//      OR building owner (+39) == active player         -> 1
//   3. selection flag bit 0x1 clear                    -> 0 (not selectable)
//   4. time window open (Building3_CheckTimeWindowOpen) -> 1
//   5. otherwise show the "closed" message            -> 0
int Building_CheckEntryAllowed(const std::uint8_t* building);

}  // namespace guild::sim
