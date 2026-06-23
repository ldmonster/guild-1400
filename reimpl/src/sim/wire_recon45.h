#pragma once
// wire_recon45 — wires the recon4 / recon5 cross-cluster bridge tables into their
// REAL reconstructed leaves (rule 13). Before this, every one of these bridges was
// fully inert at runtime: nothing in the live call tree called their Set*Hooks
// installer (only the per-module .cpp default and the unit tests did), so each
// reconstructed recon4/5 function ran against the inert defaults (find/resolve
// report "absent", every emit / stat a no-op).
//
// COVERED BRIDGES (the sub-callee hook structs of the recon4/5 clusters):
//   * Recon4ResolveHooks  (command_recon4_resolve.h)   — the VIBE_Command_ResolveTarget*
//                          console/history target-resolution pick/scan resolvers.
//   * Recon4SenderHooks   (command_recon4_senders.h)    — the VIBE_Command_SendEntityAction*
//                          packet emitters.
//   * Recon5StatHooks     (gamelogic_recon5_resolve_stat.h) — the two stat-rendering
//                          ResolveTarget handlers.
//   * Recon5TurnHooks     (gamelogic_recon5_turns.h)    — the per-tick player-turn loop.
//   * Recon5CertHooks     (gamelogic_recon5_certificate.h) — the master-certificate
//                          form populator (UI-only; documented fully inert).
//
// The Recon2Hooks bridge (character_recon2_cmds.h) is a SEPARATE cluster — the
// VIBE_Character Cmd* script dispatchers — whose entire leaf surface is scene-graph
// / matrix-transform / object / script-VM coupling that is deferred (rule 8). It is
// NOT a recon4/5 sub-callee bridge, so it is out of scope here and intentionally
// left alone (see wire_recon45.cpp for the rationale).
//
// REAL leaves bound by this installer:
//   * VIBE_Person_FindRecordById        @0x58bc6c -> PersonFindRecordById (entity.h)
//   * word_12CE910 / byte_12CE912 / dword_12CE914 / byte_12CE918 person-selection
//     table columns -> the real g_persons[] / g_personIds[] arrays (entity.h),
//     read by the exact byte offsets the originals use.
//   * VIBE_Math_RandomModulo            @0x58b89c -> util::RandomModulo
//   * the balance-delta emit (BeginDeltaPacket / AppendRawField(delta,36) /
//     QueueRequestState22) -> DeltaWriter + QueueRequestState22 on the shared
//     RealCommandQueue() (command_codec.h).
//
// Leaves with NO clean reconstructed target stay null (= each module's inert
// default) and are documented per bridge in wire_recon45.cpp:
//   - ComputeTotalWealth: the real VIBE_Person_ComputeTotalWealth (inventory_wealth.h)
//     needs the live currency ContainerView + owned-building worth list; the narrow
//     (slotIndex) hook does not carry them, so binding it would require fabricating
//     empty inputs (a cheap analogue, rule 8). Left inert.
//   - person/object QUERY graph leaves (personQueryBegin, queryByGoodType,
//     queryFindBuilding, child iterators): live object-graph state, no clean callable.
//   - StripNameTokens / ParseInt / RenderFormattedMessage / ConvertToDisplayCoord:
//     text-format + console-render leaves (render seam), inert.
//   - UI dispatchers (mapViewPanel, officeOverview) + the entire certificate Form/
//     Text/Object subsystem (rules 3-5 / UI seam): inert.
//   - the per-tick AI leaves (refillTavernStock @0x45f0a0, updateGuardBehavior
//     @0x452d38) belong to the deferred AI/scene cluster (rule 8): inert.
//   - process-global gates with ambiguous live meaning (totalPlayerCount dword_63C744,
//     guardSuppressed word_63C740, ownedGate composite, diffMinutes over the raw
//     14-byte clock image which is NOT the parsed GameTime record): inert.
namespace guild::sim {

// Install the real bindings into the recon4/5 bridge tables. Idempotent; each table
// is process-lifetime storage the module's global hook pointer references. Each is
// SEEDED from its module inert defaults (GetRecon*Hooks()) and only the wireable
// fields are overridden, so unbound fields keep their safe non-null stubs.
void InstallRealRecon45Wiring();

} // namespace guild::sim
