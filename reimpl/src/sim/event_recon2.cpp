// gilde.exe — Game-event / NPC-event state-machine cluster (recon batch 2).
//
// Implementation/anchor TU for src/sim/event_recon2.h. All reconstructed cores
// are header-inline (register-pure, deterministic), so this file mainly records
// the *deferred coupled leaves* (rule 8: documented, never faked) and the
// dispatcher wiring discovered via xrefs_to.
//
// ---------------------------------------------------------------------------
// WIRING (per xrefs_to, gilde.exe):
// ---------------------------------------------------------------------------
// The four NPC-event "step" handlers are installed by
//   VIBE_CharAction_RegisterHandlerTable @0x4db940 (data refs):
//     0x4dbe56 -> VIBE_NpcEvent_BardCreateScriptStep      (0x4d66b4)
//     0x4dbeaa -> VIBE_NpcEvent_RunSimDiseases            (0x4d766c)
//     0x4dbf36 -> VIBE_NpcEvent_BroadcastWinnerPointsStep (0x4d9a60)
//     0x4dbf8a -> VIBE_NpcEvent_OfficeMatchmakingStep     (0x4da978)
// The game-event handlers (Init/Alloc/OpenHelp/OpenBuildingDialog) are installed
// by VIBE_Event_RegisterHandlerTable @0x4f1ed0 via VIBE_He_RegisterHandlerByType:
//     type 0x6E -> {VIBE_Event_RequestGuardInteraction, VIBE_Event_InitBetriebRun}
//     type 0x85 -> {VIBE_Event_SetActionAnim7, VIBE_Event_OpenBuildingDialogRun}
//     type 0x83 -> {VIBE_Event_OpenHelpEventsForKind, VIBE_Event_HelpTextPlaybackRun}
//   (VIBE_Event_RegisterHandlerTable itself is invoked from
//    VIBE_He_InitHandlerTable @0x4c5248.)
//   VIBE_Event_AllocProduktion / _AllocWorkActorAction / _AllocSlotResetAction /
//   _AllocGebaeudeBauen are the per-type "init" callbacks installed for their
//   respective He action types and invoked by the He dispatcher when an entry of
//   that type is first stepped.
//   VIBE_Event_BroadcastFamilyNews @0x58c92c is called from
//   VIBE_Event_ConversationSinkRun @0x4efd88 (0x4f0053, 0x4f0098).
//
// Each handler entry is the He record (base pointer `a1`) with these fields used
// by this cluster (offsets from the decompile):
//   +4    owner/person object id        +8    slot index (u16)
//   +82   game-time qword (day/hour/min) +86   minute field (u16)
//   +112  state machine selector (i32)   +120  flag byte (bit2/bit4 gates)
//   +172  target object id / mode flag   +176  per-run stride / aux field
//   +180  processed counter / duration   +188  building id
//
// ---------------------------------------------------------------------------
// DEFERRED COUPLED LEAVES (reported, intentionally not reconstructed here):
// ---------------------------------------------------------------------------
//   VIBE_Event_OpenHelpEventsFromIni 0x4f1850 — file-stream INI parser ending in
//     MessageBoxA on failure: a Win32/UI + file-I/O leaf. Its caller
//     OpenHelpEventsForKind's *selection logic* IS reconstructed (HelpEventIni*).
//   VIBE_Event_SpawnBuildEffectByName 0x4f58d4 — loads/places scene object groups
//     (VIBE_Scene_LoadObjectGroup, VIBE_Object_SetPosition, light cache): a
//     scene/Vulkan-layer leaf. No extractable closed-form numeric core.
//   VIBE_Event_BroadcastFamilyNews 0x58c92c — entirely a text-message builder +
//     command/delta-packet emitter (VIBE_Text_RenderFormattedMessage,
//     VIBE_He_SendEntityMessage, VIBE_Command_*). UI/world leaf — its only
//     "logic" is iteration/category gates over the live entity arrays.
//   VIBE_Event_RegisterHandlerTable 0x4f1ed0 — a flat sequence of
//     VIBE_He_RegisterHandlerByType calls (function-pointer table population).
//     Pure dispatch wiring; belongs to the He dispatcher module, reported above.
//   VIBE_NpcAction_HandleAccidentRandom 0x4c9ac4, VIBE_He_SendEntityMessage
//     0x4c5c54, VIBE_Command_QueueRequest* , VIBE_Building_PickRandomDiseaseEvent
//     0x58aaf4, VIBE_StatChart_BuildOfficialComparison 0x58beb8,
//     VIBE_Office_CollectByCategory 0x47f03c — world mutation / record access /
//     UI; deferred to their owning modules. This cluster reconstructs the pure
//     trigger / step-transition / RNG-outcome arithmetic that drives them.

#include "sim/event_recon2.h"

namespace guild::sim {

// Anchor symbol so the translation unit is non-empty and links cleanly even
// when every numeric core is header-inline. (Mirrors the convention used by the
// sibling event/AI recon TUs.)
extern const char kEventRecon2BatchTag[];
const char kEventRecon2BatchTag[] = "gilde.exe event_recon2 (Event/NpcEvent)";

} // namespace guild::sim
