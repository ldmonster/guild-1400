#pragma once
// wire_economy2 — binds the still-inert building-economy / master-supervision hook
// bridges to their real reconstructed cross-cluster leaves (rule 13 glue only — no
// module logic). Before this installer ran, NOTHING in the live call tree installed
// these bridges, so the master-AI daily sweep and the building stock/value math ran
// against their inert default leaves.
//
// Bridges touched (grep-verified: none was previously installed by any wire_*/
// real_hooks/app installer):
//   * ai::SupervisionHooks      (ai/meister_supervision.h)  — POD fn-ptr table
//   * sim::IStockHooks          (sim/building_stock.h)      — virtual hook class
//
// SEED-FROM-DEFAULTS: SupervisionHooks is seeded from GetSupervisionHooks() (its
// non-null inert stubs) and only the cleanly-bindable fields are overridden, exactly
// like sim/wire_charaction.cpp — because the module's BindHooks() re-fills any null
// field from the defaults anyway, and several supervision callers invoke the hooks
// without a null-check. IStockHooks is a virtual class; its "seed-from-defaults" is a
// subclass whose un-overridden methods fall through to IStockHooks' inert base
// behaviour.
//
// Bridges deliberately NOT installed here (zero cleanly-bindable fields — see .cpp):
//   * sim::IStorageHooks        (sim/building_storage.h)
//   * sim::IStorageRoomHooks    (sim/buildingtype_recon.h)
//   * sim::SocialHooks          (sim/character_social.h)

namespace guild::world {

// Install the real economy-2 wiring. Idempotent; safe to call once at the
// command-system bring-up point (alongside the other InstallReal*Wiring passes).
void InstallRealEconomy2Wiring();

} // namespace guild::world
