#pragma once
// wire_meister_loc — binds the MeisterAi / Location-dialog / mission-name bridges to
// their real reconstructed leaves (rule 13). Glue only — no module logic.
//
// Covers three bridges (a fourth, LocationDialogHooks, has ZERO bindable fields and
// is intentionally left untouched — see the .cpp report comment):
//
//   * ai::Meister3Hooks          (meisterai3.h)  — MeisterAi command emitters.
//       request_build_op90 -> sim::RequestBuildOp90   (command_apply7 @0x495b58)
//       queue_coord27      -> sim::QueueRequestCoord27 (command_codec  @0x494878)
//     The remaining four (queue_slot_reset28 / emit_group_state /
//     update_handler_worldpos / dispatch_handler) are abstracted-shape mismatches
//     against the reconstructed full builders or unreconstructed dispatch tables ->
//     left inert (documented in the .cpp).
//
//   * world::LocationDialog4Hooks (location4.h) — extra dialog-body side effects.
//       dungeonHasJailer   -> world::OfficeGetEntryByHolder(17,..) != 0  (office @0x47ef28)
//     The rest need live skill/law/money state or process-global current-actor
//     filters -> left inert.
//
//   * gui::MissionNameHooks       (mission_load_run.h) — VIBE_Building_LookupTypeRecordA.
//       LookupBuildingTypeRecord -> sim::Building_LookupTypeRecordA (building2 @0x589778),
//     returning the 6-byte record (the original's qmemcpy length).
//
// SEED-FROM-DEFAULTS: the function-pointer bridges are seeded from their module's
// inert defaults (GetMeister3Hooks / GetLocationDialog4Hooks) and only the wireable
// fields overridden, so unbound fields keep their safe stubs. The two virtual-class
// bridges (MissionNameHooks) bind by installing a process-lifetime subclass instance.

namespace guild::world {

// Installs the real MeisterAi / LocationDialog4 / MissionName leaf bindings. Idempotent
// at process scope (the hook structs / subclass instances live for the process).
void InstallRealMeisterLocWiring();

} // namespace guild::world
