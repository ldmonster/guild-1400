#pragma once
// wire_actionops — wires the action-ops bridge tables that this agent owns into
// their REAL reconstructed cross-cluster leaves (rule 13). The eight bridges this
// agent surveyed are:
//
//   SetKillHooks          (render/particle_spawn.h)   KillHooks
//   SetMiscActionHooks    (sim/charaction_misc.h)     MiscActionHooks
//   Selection_SetResetHooks (play/input_recon_select.h) SelectionResetHooks
//   Menu_SetRunHooks      (gui/main_menu_run.h)        MainMenuRunHooks
//   SetWaitHooks          (sim/command_apply9.h)       WaitHooks
//   OfficeSetFlowHooks    (world/office_law3.h)        OfficeFlowHooks
//   SetOps4Hooks          (io/file_ops4.h)             FileOps4Hooks
//   SetOp85Hooks          (sim/command_apply9.h)       Op85Hooks
//
// Before this installer, none of these bridges were ever installed by the live call
// tree (only their own .cpp definitions + the per-module unit/e2e/integration tests
// call their Set*Hooks installers — verified by grep over src/), so every one ran
// against its inert module default.
//
// Of the eight, seven are ZERO-bindable — every field is a pre-approved platform /
// render / network / process-global-table boundary (rules 3-6) with NO clean
// reconstructed callable target, so per the wiring rule ("ZERO-bindable bridge ->
// create nothing") they get no installer here. The reasons, per bridge:
//
//   * SetKillHooks: isValid (VIBE_Memory_IsValidPointer @0x4391f0), freeNode
//     (VIBE_Render_FreeObjectNode @0x5e0f30), report (VIBE_Script_ReportError) —
//     all memory/render/script boundary leaves.
//   * Selection_SetResetHooks: personQueryBegin (VIBE_Person_QueryBegin varargs
//     filter — pairing unknowable, rule 8), gameObjectQueryFind/IterNext
//     (entity-query iterators over process-global arrays), objectTypeByte
//     (dword_13CE27C side table) — no standalone callable target.
//   * Menu_SetRunHooks: the main-menu RUN bridge fields are window/render/menu
//     frame-loop leaves (rules 3-5 backend seam).
//   * SetWaitHooks: pump (FlushSendQueue/ReceiveAndQueue), receivedHead/nextNode
//     (the received-list whose +149 link cannot survive a 64-bit host), gameTick
//     (dword_62EB38) — network/clock process-global.
//   * OfficeSetFlowHooks: tryPromote (VIBE_Office_TryPromoteCharacter), personType,
//     refresh (VIBE_Amt_RefreshGuildState), packetStatus
//     (VIBE_Command_GetPacketStatusById) — office/network flow leaves.
//   * SetOps4Hooks: every field is a raw OS file primitive (lseek/read/write/
//     chsize/open/GetLastError/GetFileType/FindFirst/FindClose/flush-free/lock) —
//     the rule-4/rule-6 file I/O boundary.
//   * SetOp85Hooks: unitXform — a cross-module combat-unit struct walk
//     (*(*(unit+388)+52)+{...}) with no standalone reconstructed leaf.
//
// The eighth — SetMiscActionHooks — has exactly ONE field with a genuine
// reconstructed callable target:
//
//   * findNearby  (VIBE_Character_FindNearbyInRadius @0x40507c)
//        -> guild::sim::FindNearbyInRadius (character_social.h)
//
// confirmed against the sole caller VIBE_CharAction_RotateInterpolate @0x40b998:
//   `VIBE_Character_FindNearbyInRadius(v2, (int)v19, 30.0)` — i.e. the array-filling
//   scan (self, outBuf, radius) whose nonzero return is used as a boolean and whose
//   v19[0] first hit is then touched. The MiscActionHooks `findNearby` field is the
//   single-result form of that same leaf, so we bind it through a thin adapter that
//   replays the array scan and surfaces the first hit (or null).
//
// All other MiscActionHooks fields stay at their inert module default (NON-null
// stubs) and are documented inert in wire_actionops.cpp: the anim / motion / sample
// / scene-slot / heightmap leaves (attachMovementAni, attachAni, stepMotionQueue,
// checkQueueReady, stopSample, sceneSlotIndex, worldToTile). Their reconstructed
// cousins (AttachAni/AttachMovementAni take CharActor3*, CheckQueueReady/StopSample
// take ChActor*, StepMotionQueue takes MotionQueueNode*) operate on DIFFERENT
// runtime structs than the Character* the hook surface carries; a Character* ->
// CharActor3*/ChActor*/MotionQueueNode* reinterpret would NOT be byte-faithful
// (unlike the He/HandlerRecord/Person record-base aliasing), so they stay inert
// (rule 8: omit rather than fake).
namespace guild::sim {

// Install the real binding into the MiscActionHooks bridge (the one bridge of the
// eight surveyed with a bindable field). Seeds the table from the module's inert
// defaults (non-null stubs — several CharAction step call sites invoke the hooks
// WITHOUT a null-check) and overrides only `findNearby`. Idempotent; the table is
// process-lifetime storage the global hook pointer references.
void InstallRealActionOpsWiring();

} // namespace guild::sim
