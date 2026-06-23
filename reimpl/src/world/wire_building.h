#pragma once
// =============================================================================
// guild::world — REAL BUILDING-MODULE WIRING.
//
// Installs the reconstructed building bridges into their live process-global hook
// slots so the 1:1-reconstructed building control flow runs in the real game
// (rule 13). Before this, every building bridge was inert AT RUNTIME: nothing in
// the live call tree installed a hook table, so the reconstructed functions never
// ran in production (only tests installed mocks).
//
// Each building bridge already defaults its global hook pointer to a static
// "inert default" instance whose virtual leaves reproduce the originals'
// no-subsystem behaviour AND, where a real reconstructed sibling exists, route
// through it internally (Building3 -> BuildingType_GroupFromCode; Building4 ->
// MapTypeToCategory / IsProductionType; Building5 -> StrCmpNoCaseN / StrncmpN /
// VectorWithinTolerance / PointThroughBoneChain / Building3_LookupTypeName;
// Building6 -> Building3_CheckTimeWindowOpen / MapKindToCategory). So merely
// installing the inert default (Set*Hooks(nullptr)) already makes the
// reconstructed control flow live over inert sub-leaves — the established
// headless-faithful wiring pattern (cf. sim::InstallRealReaperWiring).
//
// Two bridges gain a REAL leaf binding beyond the inert default:
//   * Building5Hooks::GameTimeAdvance — bound to the REAL sim::GameTimeAdvance
//     (gilde.exe 0x583150). The inert default is a no-op; the original gate-sync
//     code schedules future appointment times through this leaf, so binding it
//     makes Building_RequestGateFlagSync / Building_ResetGateState advance the
//     real packed time record.
//   * BuildingDialogHooks::checkEntryAllowed — bound to the REAL building entry
//     gate sim::Building_CheckEntryAllowed (gilde.exe 0x51dcd4), resolving the
//     dialog's building id through the REAL sim::BuildingFindById (0x587b20).
//
// All other building leaves route to genuinely UNRECONSTRUCTED cross-module
// subsystems (the GameObject/Person scene query iterators, the He_* handler
// registry/enumerator, the Command net channel, the selection-flag bitfield, the
// Inventory subsystem, the UI message box). They stay inert by design — there is
// no reconstructed target to bind, so they remain the originals' null-subsystem
// no-ops (rule 8: no cheap analogue).
// =============================================================================
namespace guild::world {

// Install the real building wiring into every building bridge's global hook slot.
// Idempotent; safe to call once at startup. See wire_building.cpp for the exact
// per-bridge bindings.
void InstallRealBuildingWiring();

} // namespace guild::world
