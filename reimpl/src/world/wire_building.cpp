// =============================================================================
// guild::world — REAL BUILDING-MODULE WIRING implementation. See wire_building.h.
//
// Additive: NEW file only. No owned building*.cpp / wiring.cpp is edited; this TU
// only calls the existing public Set*Hooks entry points.
// =============================================================================
#include "world/wire_building.h"

#include <cstdint>

#include "guild/common/types.h"

// The seven building bridges.
#include "sim/building2.h"           // IBuilding2QueryHooks / SetBuilding2QueryHooks
#include "sim/building3.h"           // Building3Hooks       / SetBuilding3Hooks
#include "sim/building4.h"           // Building4Hooks       / SetBuilding4Hooks
#include "sim/building5.h"           // Building5Hooks       / SetBuilding5Hooks
#include "sim/building6.h"           // Building6Hooks       / SetBuilding6Hooks
#include "sim/building_value.h"      // IBuildingRatingHooks / SetBuildingRatingHooks
#include "play/interact_building.h"  // BuildingDialogHooks  / SetBuildingDialogHooks

// REAL reconstructed siblings bound by this wiring.
#include "sim/gametime.h"            // sim::GameTimeAdvance        (0x583150)
#include "sim/entity.h"             // sim::BuildingFindById       (0x587b20)
#include "sim/buildingtype_callers.h" // sim::WireBuildingCallers (0x586fb8/0x496b90)
#include "sim/command_apply2.h"     // sim::SetEstateTransferHook  (opcode 0x0D seam)
#include "world/estate_transfer.h"  // world::PersonTransferEstateOwnership (0x58c4a8)
#include "sim/building_lifecycle.h" // SetBuildLifecycleHooks / SetBuildingFrameHooks
// Building_CheckEntryAllowed (0x51dcd4) is declared in building6.h (above).

namespace guild::world {

namespace {

// ---------------------------------------------------------------------------
// Building5: a subclass that overrides the single leaf for which a REAL
// reconstructed sibling exists — VIBE_GameTime_Advance (gilde.exe 0x583150). The
// hook receives the packed 14-byte time record as a raw byte pointer (the
// original addresses the building record's +82 appointment block); sim::GameTime
// is exactly that packed record, so we bind through a reinterpret_cast. Argument
// order matches the original's (addDays, addSeconds, addMinutes). Every other
// Building5 leaf stays the inert default (genuinely unreconstructed subsystems).
// ---------------------------------------------------------------------------
struct RealBuilding5Hooks : guild::sim::Building5Hooks {
    std::int32_t GameTimeAdvance(std::uint8_t* timeRec, int addDays,
                                 int addSeconds, int addMinutes) override {
        if (!timeRec) return 0;
        // gilde.exe 0x583150 — VIBE_GameTime_Advance over the packed time record.
        return guild::sim::GameTimeAdvance(
            reinterpret_cast<guild::sim::GameTime*>(timeRec),
            addDays, addSeconds, addMinutes);
    }
};

// ---------------------------------------------------------------------------
// BuildingDialog: bind checkEntryAllowed to the REAL building entry gate. The
// dialog FSM (play::BuildingDialogFsm::Open) hands a building id + kind; the
// gate (gilde.exe 0x51dcd4) wants the live building record, which the REAL
// sim::BuildingFindById (0x587b20) resolves. enterInterior / applyFieldDelta are
// left null so the module's own reconstructed defaults run (the apply default
// mutates the live BuildingRec; the interior load is the inert GUI no-op).
// ---------------------------------------------------------------------------
bool RealDialogCheckEntry(guild::i32 buildingId, int /*kind*/) {
    guild::sim::ObjectRec* o = guild::sim::BuildingFindById(buildingId);
    if (!o) return false;   // no live record -> deny entry (stale target).
    // gilde.exe 0x51dcd4 — VIBE_Building_CheckEntryAllowed.
    return guild::sim::Building_CheckEntryAllowed(
               reinterpret_cast<const std::uint8_t*>(o)) != 0;
}

// gilde.exe 0x496888 — VIBE_Command_ExBindObjectProto (apply opcode 0x0D) calls
// VIBE_Person_TransferEstateOwnership(*(a1+16) /*fromId*/, *(int**)(a1+20) /*TO
// id*/, a2 /*ctx*/). command_apply2's ExBindObjectProto passes (fromId, spec =
// pkt.bytes+0x14), so spec[0] IS the TO id; the original's a3 ctx is the
// command-dispatch context (0 on the apply jump-table path). Bind the inert
// estate-transfer hook (default returned 0) to the REAL reconstructed
// world::PersonTransferEstateOwnership (0x58c4a8) so the apply mutates the live
// person/building ownership tables. (rule 13)
guild::i32 RealEstateTransfer(guild::i32 fromId, const guild::i32* spec) {
    return guild::world::PersonTransferEstateOwnership(fromId, spec[0], /*ctx=*/0);
}

} // namespace

void InstallRealBuildingWiring() {
    // -- Process-lifetime hook instances the global slots reference. ----------
    static RealBuilding5Hooks            s_building5Hooks;
    static guild::play::BuildingDialogHooks s_dialogHooks = [] {
        guild::play::BuildingDialogHooks h{};
        h.checkEntryAllowed = &RealDialogCheckEntry;  // REAL entry gate (0x51dcd4)
        // enterInterior / applyFieldDelta stay null -> module reconstructed defaults.
        return h;
    }();

    // -- Bridges whose inert default already routes the reconstructed control --
    //    flow over the reconstructed table siblings; install the default so the
    //    reconstruction is LIVE (Set*Hooks(nullptr) restores the static default).
    guild::sim::SetBuilding2QueryHooks(nullptr);   // VIBE_Building_* query slice
    guild::sim::SetBuilding3Hooks(nullptr);        // occupancy / lifecycle slice
    guild::sim::SetBuilding4Hooks(nullptr);        // defaults / storage slice
    guild::sim::SetBuilding6Hooks(nullptr);        // CheckEntryAllowed gate
    guild::sim::SetBuildingRatingHooks(nullptr);   // production-rating math

    // -- Bridges with a REAL leaf binding beyond the inert default. -----------
    guild::sim::SetBuilding5Hooks(&s_building5Hooks);     // GameTimeAdvance -> 0x583150
    guild::play::SetBuildingDialogHooks(&s_dialogHooks);  // checkEntryAllowed -> 0x51dcd4

    // -- Building/Bauplatz caller cluster (buildingtype_callers.cpp). ---------
    //    Installs the 1:1 VIBE_Building_CreateGebaeude flow (0x586fb8) as the
    //    Building_CreateGebaeude backend (live callers: command_apply5.cpp
    //    opcodes 0x0A/0x4C) and the ExSellObjekt storage-node phases (0x496b90)
    //    into trade_sell's commit path (live caller: command_apply6.cpp 0x11).
    guild::sim::WireBuildingCallers();

    // -- Estate-transfer apply hook (command_apply2 opcode 0x0D seam). --------
    //    gilde.exe 0x496888 -> 0x58c4a8. Was inert (returned 0); now LIVE.
    guild::sim::SetEstateTransferHook(&RealEstateTransfer);

    // -- Building lifecycle / per-frame bridges (wave-19 building cluster). ----
    //    Installing the inert defaults (Set*Hooks(nullptr)) makes the reconstructed
    //    control flow LIVE over inert sub-leaves (the established headless-faithful
    //    pattern): VIBE_Building_Update (0x40e2b4, frame-loop object sweep) ordering
    //    its StateUpdate->AnimationBasic step, and VIBE_Building_CheckBuildRequirements
    //    (0x587bfc) running its category/room state machine. The genuinely
    //    unreconstructed scene/window leaves stay the originals' no-ops (rule 8).
    //    The OpenUpgradeWindow charge math (Building_ComputeUpgradeChargeAmount,
    //    0x50f7c0) + node classifier (Building_ClassifyUpgradeNode) take their hooks
    //    as direct args and have no live modal-window caller yet (the modal frame
    //    loop is unreconstructed) — they are reachable for that handler when it lands.
    guild::sim::SetBuildLifecycleHooks(nullptr);  // CheckBuildRequirements live
    guild::sim::SetBuildingFrameHooks(nullptr);   // Building_Update (0x40e2b4) live
}

} // namespace guild::world
