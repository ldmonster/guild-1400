#pragma once
// wire_charaction — wires the five CharAction step / NpcAction-recon bridge tables
// into their REAL reconstructed cross-cluster leaves (rule 13). Before this, all
// five bridges were fully inert at runtime — nothing in the live call tree ever
// called their Set*Hooks installers (only the .cpp definitions and the per-module
// unit/e2e tests did), so every CharAction step coroutine these bridges drive ran
// against the inert default (free/find/resolve report "absent", every emit a no-op).
//
// This installer binds, for each of the five bridges, the leaf fields that have a
// genuine reconstructed callable target — exactly the established real-wiring
// pattern (cf. sim/real_hooks3.cpp, which binds the NpcLeafHooks/NpcAction*Hooks
// free/find/emit leaves and documents the rest as stubs):
//
//   * VIBE_He_FreeHandlerEntry          @0x4c6144  -> HandlerTable::FreeHandlerEntry
//   * VIBE_He_FindFirstHandlerByFilter  @0x4c63f8  -> HandlerTable::FindFirstHandlerByFilter
//   * VIBE_He_FindNextMatchingHandler   @0x4c6278  -> HandlerTable::FindNextMatchingHandler
//   * VIBE_Person_FindRecordById        @0x58bc6c  -> PersonFindRecordById (entity.h)
//   * VIBE_GameObject_ResolveEntityById @0x583b44  -> GameObjectResolveEntityById (entity.h)
//   * VIBE_Math_RandomModulo            @0x58b89c  -> util::RandomModulo
//   * VIBE_Building_MapTypeToCategory   @0x5878b0  -> Building_MapKindToCategory
//   * VIBE_Command_QueueRequestEntity29 @0x4949c4  -> QueueRequestEntity29 (recon)
//   * VIBE_Command_QueueRequestArgs25   @0x494810  -> QueueRequestArgs25
//   * VIBE_Command_QueueRequestCoord27  @0x494878  -> QueueRequestCoord27
//   * VIBE_Command_QueueRequestQuad60   @0x495124  -> QueueRequestQuad60
//
// The shared real He/HandlerEntry pool (RealHandlerTable()) and command queue
// (RealCommandQueue()) are the SAME singletons real_hooks3 owns, so the wired
// CharAction steps free/find/emit against the one live game state, exactly like the
// NpcAction bridges already do.
//
// Leaves with NO clean reconstructed target stay null (= inert default) and are
// documented per bridge in wire_charaction.cpp:
//   - person-query (personQueryBegin/personIterNext): the real PersonQueryBegin
//     (entity.h) takes a (op,value) PersonFilter list; the hook surface is three
//     bare ints whose op/value pairing the originals build from a stack buffer the
//     decompile does not unambiguously expose — binding it would require GUESSING
//     the pairing (rule 8). Left inert.
//   - process-global side tables not modeled as standalone callable leaves:
//     statTableByte (byte_12CE990), cityWillingness/cityCategory/cityPersonId
//     (byte_12CE990/byte_12CE912/dword_12CE914), realTimeModeFlag/fastTimeEnabled
//     (dword_63C7B8). No live instance exists outside the per-record models, so
//     these stay inert (default 0 / not-fast), exercising the faithful control flow.
//   - render / event-panel / dialog-result / voice / person-card UI leaves
//     (rules 3-5 backend seam): eventPanelCreate/Destroy, renderDialogLine,
//     playVoice, buildPersonCard, dialogWindow, dialogResult, sendNotifyMessage,
//     sendEntityMessage/sendQuickjumpMessage, etc.: opaque render-bridge, inert.
//   - emits needing argument reconstruction the hook shape collapses away
//     (enqueueCmd15 opcode-15, registerApEvent, requestBuildOp73Sabotage, the
//     GuildJoin packet builders, the office-candidacy leaves): inert.
namespace guild::sim {

// Install the real bindings into all five CharAction step / NpcAction-recon
// bridges (CharActionStep2/3/4Hooks, CharActionReconHooks, CharActionRecon2Hooks).
// Idempotent; each table is process-lifetime storage the global hook pointer
// references. Composes with InstallRealSimHooks3 (shares RealHandlerTable() /
// RealCommandQueue()).
void InstallRealCharActionWiring();

} // namespace guild::sim
