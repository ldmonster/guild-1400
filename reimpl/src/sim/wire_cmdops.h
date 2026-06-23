#pragma once
// wire_cmdops — the command-apply / action-op bridge wiring (rule 13: wire
// reconstructed leaves into their inert hook bridges). One installer,
// InstallRealCmdOpsWiring(), that SEEDS each target bridge from its module inert
// defaults and overrides ONLY the fields that have a clean, byte-faithful
// reconstructed leaf to bind to.
//
// Bridges surveyed by this agent (grep src/sim + src/io + src/render):
//
//   * OrderDriverHooks   (sim/combat_orders2.h)   — the per-slot order-tick driver
//     (VIBE_Combat_UpdateUnitOrders @0x491688) scene/path/command leaves. ONE
//     bindable field: `randomModulo` -> the REAL guild::sim::Math_RandomModulo
//     (gilde.exe 0x58b89c, over crt::RandNext). The remaining fields
//     (packetStatus / resolveUnit / onTargetTile / findFreeTile / unitBusy / emit)
//     are scene/path/command/sink leaves with inert deterministic defaults and no
//     standalone reconstructed callable target -> left inert.
//
// Bridges surveyed and found to have ZERO bindable fields (no code emitted for
// them, per the agent constraint; documented here + in the final report):
//
//   * Op85Hooks          (sim/command_apply9.h)   — `unitXform`: the
//     VIBE_Command_RequestBuildOp85Unit @0x4959a8 cross-module struct walk
//     (*(unit+388) -> *(...+52)+{76,80,84,132,136,140}, plus *(unit+36)). No
//     reconstructed leaf performs this exact native-CombatUnit scene-node pose
//     walk as a callable function (only struct-field docs / abstracted per-module
//     hooks exist). Binding would not be byte-faithful -> inert.
//   * Ops4Hooks          (io/file_ops4.h, FileOps4Hooks) — every field is a raw OS
//     I/O primitive (lseek/read/write/chsize/open/GetLastError/GetFileType/
//     FindFirstFile/FindClose/flush-and-free/CRT-table lock). File I/O is rule 6
//     (ASK FIRST before substituting); none is reconstructed -> all inert.
//   * MiscActionHooks    (sim/charaction_misc.h)  — attachMovementAni/attachAni/
//     stepMotionQueue/checkQueueReady/stopSample/sceneSlotIndex/worldToTile/
//     findNearby. Reconstructed siblings exist (character_render3/5.h,
//     character_social.h) but ONLY over DISTINCT decoded C++ view structs
//     (CharActor3 / ChActor) whose layouts differ from this bridge's `Character`
//     (reconstruction-only structs, NOT byte-faithful views over one native
//     record). A reinterpret_cast between them is UB / wrong offsets -> inert.
//   * KillHooks          (render/particle_spawn.h) — isValid
//     (VIBE_Memory_IsValidPointer @0x4391f0) / freeNode
//     (VIBE_Render_FreeObjectNode @0x5e0f30) / report (VIBE_Script_ReportError).
//     None reconstructed as a standalone callable leaf (only appear as inert hook
//     fields in other modules / comments) -> inert.
//   * Leaves9Hooks       (render/render_leaves9.h, RenderLeaves9Hooks) —
//     FrameDataProcess (VIBE_FrameData_Process @0x5d781c) IS reconstructed
//     (render/animation_decode) but with an INCOMPATIBLE typed signature: the
//     reconstruction's 4th arg is a decoded `FrameBlitState&`, while this bridge
//     passes the raw register `int a4` (the surface pointer) — not a byte-cast of
//     FrameBlitState. ConvertRgbTo16 (@0x5d7c0c) / Convert8To16 (@0x5d7924) ARE
//     now reconstructed (render/shape_convert16) and installed into the leaves9
//     slots by world::InstallRealWorldNetWiring — this bridge leaves them alone.
//
// Glue only — no module logic. SEED-FROM-DEFAULTS so unbound fields keep their
// safe inert stubs.

namespace guild::sim {

// Install the real command-op / action-op wiring. Seeds OrderDriverHooks from its
// inert defaults and binds the one byte-faithful reconstructed leaf
// (randomModulo -> Math_RandomModulo). Idempotent; process-lifetime hook table.
void InstallRealCmdOpsWiring();

} // namespace guild::sim
