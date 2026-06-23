#pragma once
// ===========================================================================
// wire_production.{h,cpp} — install-glue that binds the production / personnel-2
// cross-cluster bridges to their real reconstructed leaves (namespace guild::world).
// ===========================================================================
// Three previously-inert hook bridges are wired here (house vtable-hook pattern):
//
//   IProductionSlotHooks  (sim/production_slots.h)
//       The production-timer tick (VIBE_Inventory_TickProductionTimers 0x54f168).
//       BOUND: DiffMinutes -> VIBE_GameTime_DiffMinutes (gametime.h);
//              EmitProductionFinished -> VIBE_Command_QueueRequest17 on the shared
//                                        real CommandQueue (command_codec.h);
//              MarketPrice stays the inherited REAL default (Building_ComputeMarketPrice).
//       INERT: QueryObjectNode / FindGridSlot (scene-graph queries),
//              NotifyProductReady (VIBE_He_SendEntityMessage transport).
//
//   IProductionHooks      (sim/building_production.h)
//       Per-tick slot worker/stock + standalone net sync. No clean reconstructed
//       leaf — installed as the real default-backed object (live, owned, not
//       dangling). INERT: SlotWorkerOutput / SlotStoredQuantity (scene/Person-iter),
//       SyncProductionState / RandomizeStockTransforms (network sync, rule 6).
//
//   PersonPersonnel2Hooks (sim/person_personnel2.h)
//       Person query / wealth / staff-book / GUI-widget leaves. All need state the
//       hook surface cannot supply faithfully (process-global tables, live scene
//       container walks off a raw Person*, GUI widget ops) -> all INERT (the
//       module's own default block is reasserted as the live binding).
//
// PersonnelGuiHooks (gui/personnel_gui.h) is intentionally NOT wired here: it is a
// pure GUI bridge (renderer / form / widget / command-queue / game-loop leaves).
//
// SEED-FROM-DEFAULTS throughout: only fields with a clean reconstructed target are
// overridden; every other leaf inherits its module inert default.
namespace guild::world {

// Install the real production / personnel-2 wiring. Idempotent; safe to call once
// at boot. Binds the production-tick DiffMinutes + completion-command leaves to
// their reconstructed targets and (re)asserts the inert default blocks for the
// leaves with no clean reconstructed binding.
void InstallRealProductionWiring();

}  // namespace guild::world
