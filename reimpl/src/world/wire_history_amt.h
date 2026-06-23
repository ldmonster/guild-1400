#pragma once
// ---------------------------------------------------------------------------
// wire_history_amt — installs reconstructed leaf functions into two world-cluster
// hook bridges that were inert at runtime (rule 13).
//
// Before this, nothing in the live call tree bound either table's wireable fields
// to their 1:1 reconstructions, even though those reconstructions exist:
//
//   * world::AmtEconomy2GetHooks() (amt_economy2.h) — the Amt office-administration
//     leaves (ResetGuildSlots, HighlightGuildMembers, ComputeBuildingRivalryScore,
//     ComputeOfficeRenderOffset, …) route their RNG / coord-truncation / command
//     emits through this table.  Five fields have clean, headless-safe reconstructed
//     targets and are bound here:
//       randFloatScaled -> util::RandomFloatScaled  (VIBE_Math_RandomFloatScaled
//                          0x58b910 — determinism-critical, like wire_event_office's
//                          randomModulo bind; the inert default returns a constant 0).
//       truncate        -> util::ConvertX           (VIBE_Coord_ConvertX 0x5c6b08,
//                          round-toward-zero; (i32) of the integral double).
//       queueRequest16  -> sim::QueueRequest16      (opcode 16, 0x494630).
//       queueArgs26     -> sim::QueueRequestArgs26  (opcode 26, 0x494848; the recon
//                          hands a float reputation delta, the original stages its
//                          raw 32-bit pattern — we reinterpret float->i32 bits).
//       queueCoord27    -> sim::QueueRequestCoord27 (opcode 27, 0x494878; the native
//                          builder sources its coord pair from globals flt_62EB90/
//                          dword_62EB94, not from caller args, so we pass the inert
//                          0,0 coord pair exactly as wire_charaction does).
//     All command emits stage onto the SAME shared real CommandQueue that
//     real_hooks owns (sim::RealCommandQueue()).
//
//   * world::MissionReqEventGetHooks() (mission_requirement_event_recon.h) — the
//     VIBE_MissionReq_Check* objective-goal evaluators (CheckBloodLevel,
//     CheckSkillAbove, CheckZeroValue, CheckTimeElapsed, CheckMinThresholds, …)
//     route their coupled inputs through this table.  Four fields have clean,
//     headless-safe reconstructed targets and are bound here:
//       moneyConvertToDisplayCoord    -> world::MoneyConvertToDisplayCoord
//                                        (VIBE_Money_ConvertToDisplayCoord 0x58f14c).
//       economyComputeWeightedLawScore-> world::EconomyComputeWeightedLawScore
//                                        (VIBE_Economy_ComputeWeightedLawScore
//                                        0x57a580 — the native stores a1/a2 but the
//                                        score loop ignores them, so the no-arg recon
//                                        is byte-faithful; the adapter drops a1/a2).
//       economyLoadDemandSnapshot     -> world::EconomyLoadDemandSnapshot
//                                        (VIBE_Economy_LoadDemandSnapshot 0x57a5dc;
//                                        exact float*->double signature match).
//       buildingTypeMapToActionCode   -> sim::BuildingType_MapToActionCode
//                                        (VIBE_BuildingType_MapToActionCode 0x589a7c;
//                                        the native GroupFromCode takes the type code
//                                        in al — the opaque buildingTypeRec value IS
//                                        that code byte, so we pass its low byte).
//
// Every other field of both tables stays at its inert default: they are coupled
// subsystem leaves (the live person store with raw byte-field reads, the
// office-holder slot-table mutation + delta-packet network commit, He text/UI
// messages, the 3D event-icon GPU pool) whose hook signatures take opaque live
// records that are not faithfully reachable in a headless build, with no
// signature-compatible reconstruction (rule 8 — no fake stand-ins).  Both
// dispatchers null-check / default each field, so unbound fields keep their
// established inert behaviour.
//
// Sibling history-cluster bridges with NO cleanly-bindable field are intentionally
// NOT touched here (documented in the .cpp's report):
//   * world::WorldHistory2Hooks  — every field is a render/universe icon-pool
//     teardown or an unreconstructed per-player news dispatch.
//   * world::MissionDialogHooks  — every field is a Form/Text/Voice/Audio/
//     GameLogic-frameloop UI leaf; also driven by the gui Menu_* mechanism, not the
//     headless live tree.
//   * world::GoodsDistribHooks / OfficeAddTableEntryHook — by-parameter hooks
//     supplied at each call site, not a process-wide table.
//   * world::statistic_recon_* / guildstate_recon — pure reconstructions with no
//     hook bridge at all (nothing to wire).
// ---------------------------------------------------------------------------
namespace guild::world {

// Install the available reconstructed leaves into AmtEconomy2GetHooks() and
// MissionReqEventGetHooks().  SEED-FROM-DEFAULTS: each table is read back first so
// unbound fields keep their inert defaults; only the wireable fields are overridden.
// Idempotent; safe to call more than once.
void InstallRealHistoryAmtWiring();

} // namespace guild::world
