#pragma once
// gilde.exe — Interaction RULES cores (the actor<->target eligibility evaluators
// and the mutation/command-emitting "Perform" handlers). namespace guild::sim.
//
// This file is the second half of the interaction system. interaction.{h,cpp}
// (already implemented) holds the descriptor-string parser
// (VIBE_Interaction_Handler) and the per-tick queue drain
// (VIBE_GameObject_DispatchInteractions). Here live the ~80 leaf handlers that a
// resolved interaction dispatches to:
//
//   * VIBE_Interaction_Eval*  — predicate/score evaluators. Each validates an
//     actor<->target interaction (eligibility / range / state / cash gates) and
//     RETURNS an interaction "action code" (a small int: 0 == reject, else the
//     code of the chosen action). They take a leading `result` byte (the previous
//     evaluator's verdict; if nonzero the chain short-circuits to StubReturnZero)
//     and an `armed` byte (the trigger phase), matching the __usercall ABI
//     (al = result, bl = armed).
//
//   * VIBE_Interaction_Perform* — the mutation side. Each stages a fixed command
//     buffer and emits it through the lockstep command channel (modeled as a
//     mockable command hook); returns the action code applied.
//
// The render / cutscene / UI / panel / AI-planner / He / Gesetz / Privilege leaves
// these handlers call are forward-declared and routed through mockable hooks
// (see the *Hook setters); the leaves themselves are deferred (see module report).
//
// Translated functions (addresses are absolute, imagebase 0x400000):
//   0x469da8 VIBE_AiMethod_StubReturnZero          (chain short-circuit helper)
//   0x469dcc VIBE_Interaction_EvalReturnEight
//   0x469de0 VIBE_Interaction_EvalReturnBoolNotArmed
//   0x469df4 VIBE_Interaction_EvalReturnTwo
//   0x469e08 VIBE_Interaction_EvalReturnThree
//   0x46b8b0 VIBE_Interaction_EvalRejectStub
//   0x46e8a0 VIBE_Interaction_EvalUseDoor
//   0x46ebfc VIBE_Interaction_EvalRestStub
//   0x470a18 VIBE_Interaction_EvalActionCode35
//   0x470be8 VIBE_Interaction_EvalActionCode36
//   0x46c30c VIBE_Interaction_PerformRenovate
//   0x46e1ec VIBE_Interaction_PerformSabotage
//   0x46e7b4 VIBE_Interaction_PerformBeating
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

struct ContextActor; // defined below; the event holds a drag-source pointer to one

// ===========================================================================
// Interaction-event record (the "menu entry" / pending-interaction the handlers
// receive in EDX or ECX). Field offsets recovered from the handler bodies:
//   +0x00 byte  mode   — 1 = build tooltip, 2 = passive buy gate, 3 = activate,
//                        4/2 = drag-drop (target/source) phases.
//   +0x04 dword targetId — second entity / object id referenced by the action.
//   +0x08 dword param    — extra parameter slot (e.g. EvalUseDoor sets it to 1).
//   +0x14 (..) char[]    — tooltip text scratch (RenderFormattedMessage dest).
//   +0x214/+540 dword drag-source person record ptr (read for drag phases; in the
//                      binary this is *((dword*)entry+135) == entry+540).
// The record in the binary is larger; we model the fields the rules read/write.
// ===========================================================================
// This is a live runtime UI struct (never serialized), so unlike the entity
// records we do NOT byte-pack it: the drag-source slot (+540) is a raw pointer in
// the binary, which cannot round-trip through a 32-bit field on a 64-bit host. We
// model it as a native ContextActor* and document the original 32-bit offset.
struct InteractionEventRec {
    u8   mode;            // +0x00  1/2/3/4 dispatch mode
    i32  targetId;        // +0x04  second-entity / object id
    i32  param;           // +0x08  extra param slot
    char tooltip[512];    // +0x14  RenderFormattedMessage destination buffer
    i32  lawsuitScratch;  // +0x214 (+532) scratch dword written by FileLawsuit
    ContextActor* dragSource; // +0x21C (+540) drag-source person record ptr in orig
};

// ===========================================================================
// Context actor record (the live Character/person record `a1` the ContextAction
// and several Eval handlers operate on). The binary indexes it by raw byte
// offset; the offsets below are recovered from the handler bodies. This is a
// VIEW over the live record (the same memory the Character/Person modules own);
// we model only the fields the rules touch.
//   +0x02  kind byte   (== 6 → human player char; gates tooltip rendering)
//   +0x09  gender/married flag (selects message-string base 272 vs 279 / 525/560)
//   +0x0D  rank/level byte (rank gates: Promote/Train/Equip/Demote/AssignToSlot)
//   +0x10  world/transform float referenced by Perform* (offset +16)
//   +0x14  float scale referenced by Perform* (offset +20)
//   +0x24  (+36) wealth/score int (ToggleFollow gate)
//   +0x2C  (+44) status bitfield (ToggleFollow gate)
//   +0x5C  (+92) associated person id (AssignTask / AssignToSlot)
//   +0x166 (+358) profession/role byte (the big profession/command gates)
//   +0x169 (+361) secondary profession byte (ShowDualProfession)
//   +0x1C8 (+456) flag byte (ToggleFollow: bit 0x40)
//   +0x1C9 (+457) flag byte (bit1 busy, bit0 selectable-as-target, 0x40, 0x04)
//   +0x1CA (+458) flag byte (0x08, 0x20, 0x40 gates)
// ===========================================================================
constexpr int kContextActorStride = 600;

GUILD_PACKED_BEGIN
struct ContextActor {
    u8   pad0[2];         // +0x00
    u8   kind;            // +0x02  kind byte (6 == player char)
    u8   pad3[6];         // +0x03
    u8   gender;          // +0x09  gender/married flag
    u8   pad10[3];        // +0x0A
    u8   rank;            // +0x0D  rank/level byte
    u8   pad14[2];        // +0x0E
    float world16;        // +0x10  transform (Perform*)
    float scale20;        // +0x14  scale (Perform*)
    u8   pad24[12];       // +0x18
    i32  score36;         // +0x24  (+36) wealth/score
    u8   pad40[4];        // +0x28
    i32  statusBits44;    // +0x2C  (+44) status bitfield
    u8   pad48[44];       // +0x30
    i32  assocPersonId;   // +0x5C  (+92) associated person id (-1 == none)
    u8   pad96[262];      // +0x60..+0x165 (96+262 == 358)
    u8   profession;      // +0x166 (+358) profession/role byte
    u8   pad359[2];       // +0x167
    u8   profession2;     // +0x169 (+361) secondary profession byte
    u8   pad362[94];      // +0x16A
    u8   flag456;         // +0x1C8 (+456)
    u8   flag457;         // +0x1C9 (+457)
    u8   flag458;         // +0x1CA (+458)
    u8   pad459[141];     // +0x1CB
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(ContextActor) == kContextActorStride, "ContextActor stride");

// Interaction-event dispatch modes (the `mode` byte / *a2).
enum InteractionMode : u8 {
    kModeTooltip   = 1,  // build menu-entry tooltip text
    kModePassive   = 2,  // passive/drag-source phase
    kModeActivate  = 3,  // user activated the menu entry → emit command
    kModeDragApply = 4,  // drag-drop apply phase
};

// ===========================================================================
// Mockable leaf hooks. Default behavior is a no-op that records the call so
// tests can assert which command/panel a handler would emit. Production wires
// the real Privilege/He/Gesetz/Command/Recruit/render leaves.
// ===========================================================================

// Records the most recent leaf invocation a handler made (for tests).
struct InteractionLeafTrace {
    int  privilegeLeaf;   // address-id of the VIBE_Privilege_* leaf invoked (0 none)
    int  privilegeReturn; // value the privilege hook returned
    int  commandAction;   // action code emitted by a Perform* via the command hook
    const char* commandTag; // the "renovieren"/"sabotage"/"pruegel" tag, or null
    int  tooltipStringId; // last tooltip string id rendered (mode 1), 0 none
};
extern InteractionLeafTrace g_leafTrace;
void ResetInteractionLeafTrace();

// VIBE_Privilege_* panel/command leaves (0x55xxxx/0x56xxxx range). Each ContextAction
// activate (mode 3/4) calls one. The hook receives the leaf-id + actor + event and
// returns the low result bits the caller OR's into its action code. Default: 0.
using PrivilegeLeafFn = int (*)(int leafId, ContextActor* actor, InteractionEventRec* ev);
void SetPrivilegeLeafHook(PrivilegeLeafFn fn);

// VIBE_Command_Enqueue* — the lockstep command builder the Perform* handlers emit
// through. The hook receives the action tag (e.g. "sabotage") and the staged
// action code. Default: records into g_leafTrace.
using CommandEmitFn = void (*)(const char* tag, int actionCode);
void SetCommandEmitHook(CommandEmitFn fn);

// VIBE_Person_GetCurrencyAmount @0x5915b8 — currency held by a person record for a
// given currency index. EvalUseDoor / EvalBuyObject gate on it. Default: returns a
// configurable amount (set via SetCurrencyHook). Reused conceptually from person.*.
using CurrencyAmountFn = int (*)(ContextActor* actor, u8 currencyIndex);
void SetCurrencyHook(CurrencyAmountFn fn);

// Shared executor plumbing exported for the second ContextAction batch
// (contextaction2.cpp) so both batches route through the SAME g_privilegeHook /
// g_leafTrace (ODR: defined once, here).
//   InvokePrivilegeLeaf: call a VIBE_Privilege_* leaf (records into g_leafTrace,
//     returns the leaf's low result bits the caller OR's into its verdict).
//   RecordTooltip: record a mode-1 tooltip render (string id into g_leafTrace).
int  InvokePrivilegeLeaf(int leafId, ContextActor* a, InteractionEventRec* ev);
void RecordTooltip(InteractionEventRec* ev, int stringId);

// Privilege leaf-id constants (the absolute addresses of the real leaves; used as
// stable opaque ids in the trace so tests can assert which leaf would fire).
enum PrivilegeLeaf : int {
    kPrivShowDialog          = 0x571218, // VIBE_Privilege_ShowDialog
    kPrivChangeProfession    = 0x565304, // VIBE_Privilege_PanelChangeProfession
    kPrivMedicus             = 0x560500, // VIBE_Privilege_PanelMedicus
    kPrivEvidenceReview      = 0x565f9c, // VIBE_Privilege_PanelEvidenceReview
    kPrivBlackmail           = 0x560f14, // VIBE_Privilege_PanelBlackmail
    kPrivSendBuildCmd        = 0x561a74, // VIBE_Privilege_SendBuildCmd
    kPrivSendSimpleCmd       = 0x561700, // VIBE_Privilege_SendSimpleCmd
    kPrivDivorce             = 0x5608b0, // VIBE_Privilege_PanelDivorce
    kPrivRemoveFromOffice    = 0x561fd0, // VIBE_Privilege_RemoveFromOffice
    kPrivEmbezzlement        = 0x562334, // VIBE_Privilege_PanelEmbezzlement
    kPrivCharmConfirm        = 0x5627cc, // VIBE_Privilege_CharmConfirm
    kPrivCounterEspionage    = 0x5628c8, // VIBE_Privilege_PanelCounterEspionage
    kPrivSwapSeats           = 0x562cdc, // VIBE_Privilege_PanelSwapSeats
    kPrivExpelWorker         = 0x5639f4, // VIBE_Privilege_PanelExpelWorker
    kPrivInterrogation       = 0x563614, // VIBE_Privilege_PanelInterrogation
    kPrivMakePeace           = 0x563f14, // VIBE_Privilege_PanelMakePeace
    kPrivEnactLaw            = 0x561bb4, // VIBE_Privilege_PanelEnactLaw
};

// ===========================================================================
// Eval handlers — return an interaction action code (0 == reject).
// Common ABI: (result == previous verdict; armed == trigger phase; ctx as noted).
// ===========================================================================

// gilde.exe 0x469da8 — VIBE_AiMethod_StubReturnZero (__stdcall). Returns 0.
char AiMethodStubReturnZero(int ctx);

// gilde.exe 0x469dcc — VIBE_Interaction_EvalReturnEight (al=result, bl=armed, ctx).
// If a prior verdict exists, short-circuit (StubReturnZero); else if not armed,
// fire action 8; else keep `result`.
char EvalReturnEight(char result, char armed, int ctx);
// gilde.exe 0x469de0 — VIBE_Interaction_EvalReturnBoolNotArmed.
char EvalReturnBoolNotArmed(char result, char armed, int ctx);
// gilde.exe 0x469df4 — VIBE_Interaction_EvalReturnTwo.
char EvalReturnTwo(char result, char armed, int ctx);
// gilde.exe 0x469e08 — VIBE_Interaction_EvalReturnThree.
char EvalReturnThree(char result, char armed, int ctx);
// gilde.exe 0x46b8b0 — VIBE_Interaction_EvalRejectStub. Always 0.
char EvalRejectStub();
// gilde.exe 0x46ebfc — VIBE_Interaction_EvalRestStub. Always 26.
char EvalRestStub();

// gilde.exe 0x470a18 — VIBE_Interaction_EvalActionCode35 (ecx=event, bl=armed, ctx).
// Fires 35 iff event.mode==1 && armed==3, else StubReturnZero.
char EvalActionCode35(const InteractionEventRec* ev, char armed, int ctx);
// gilde.exe 0x470be8 — VIBE_Interaction_EvalActionCode36. Fires 36 iff mode==1 &&
// armed==4.
char EvalActionCode36(const InteractionEventRec* ev, char armed, int ctx);

// gilde.exe 0x46e8a0 — VIBE_Interaction_EvalUseDoor (edx=actor, ecx=event, bl=armed,
// ctx). Range/state/flag/cash gates for "use door" (action 25). Sequence:
//   armed>1 → 0; armed==1 must already be a door-13/code-2 event;
//   actor.rank (+13) >= 2 → 0;  (actor.flag457 & 0x40)==0 → 0;
//   cash = GetCurrencyAmount(actor); cash<0 || cash*0.44 < 16000.0 → 0;
//   else event.mode=13, event.param... set, return 25.
char EvalUseDoor(ContextActor* actor, InteractionEventRec* ev, u8 armed, int ctx);

// ===========================================================================
// ContextAction executors — a context-menu entry dispatches to one of these.
// Common ABI: (eax = actor record, edx = event). Returns the menu-entry verdict:
//   1 == ineligible / not applicable (entry hidden),
//   2 == handled (single-target),
//   4 == "advance to next tier" sentinel (rank-too-high),
//   9 == busy (flag457 & 2),
//   10 (0xA) == handled (drag-drop target path),
//   plus the low bit OR'd from the privilege leaf for the "send command" ones.
// On mode 3/4 (activate) they invoke a VIBE_Privilege_* leaf (mock); on mode 1
// they render a tooltip (mock, recorded as tooltipStringId).
// ===========================================================================

// gilde.exe 0x56efb0 — VIBE_ContextAction_AssignTask.
char ContextAssignTask(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f0ac — VIBE_ContextAction_PromoteRank (rank<3 gate).
char ContextPromoteRank(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f15c — VIBE_ContextAction_OpenInventory (kind 5/6 gate).
char ContextOpenInventory(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f1c0 — VIBE_ContextAction_ToggleFollow (flag456/score gate).
char ContextToggleFollow(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f234 — VIBE_ContextAction_AssignGuard (profession 13 gate + He).
char ContextAssignGuard(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f2c0 — VIBE_ContextAction_ToggleFlag457 (flag457 & 1 gate).
char ContextToggleFlag457(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f314 — VIBE_ContextAction_TrainRank3A (rank==3 exact gate).
char ContextTrainRank3A(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f3c0 — VIBE_ContextAction_DemoteIfRank3 (rank>=3 gate).
char ContextDemoteIfRank3(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f410 — VIBE_ContextAction_EquipIfRank3 (rank>=3 && !(457&4)).
char ContextEquipIfRank3(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f468 — VIBE_ContextAction_TrainRank4A (rank==4 exact gate).
char ContextTrainRank4A(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f670 — VIBE_ContextAction_TrainRank5A (rank>=5 gate).
char ContextTrainRank5A(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f7b0 — VIBE_ContextAction_AssignToSlot (rank>=5 + free-slot + assoc).
char ContextAssignToSlot(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f9a4 — VIBE_ContextAction_ShowDualProfession (prof||prof2 gate).
char ContextShowDualProfession(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56faf0 — VIBE_ContextAction_StartWorkTask (profession!=0 + flags).
char ContextStartWorkTask(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56fe98 — VIBE_ContextAction_ProfessionMenu11 (profession==11 gate).
char ContextProfessionMenu11(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56ff58 — VIBE_ContextAction_ProfessionMenuGuard (prof 27/15/21/13).
char ContextProfessionMenuGuard(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5703f8 — VIBE_ContextAction_AttackOrSteal (prof 22/25/18 + drag).
char ContextAttackOrSteal(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5704d4 — VIBE_ContextAction_TalkOrSocialize (prof 19/20/24/26 + drag).
char ContextTalkOrSocialize(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570a1c — VIBE_ContextAction_CommandPatrol (prof 15/21/27 + drag).
char ContextCommandPatrol(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5707f0 — VIBE_ContextAction_StartGuildTask (prof==14 + flags + drag).
char ContextStartGuildTask(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570c78 — VIBE_ContextAction_CommandMultiType (prof 23/19/20/13 + drag).
char ContextCommandMultiType(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570d60 — VIBE_ContextAction_CommandType22Or25 (prof 22/25 + drag).
char ContextCommandType22Or25(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570ef8 — VIBE_ContextAction_CommandType27 (prof==27 + drag).
char ContextCommandType27(ContextActor* actor, InteractionEventRec* ev);

// ===========================================================================
// Perform* — mutation / command emitters. Stage a buffer + emit via the command
// hook; return the applied action code. The render (Light_SetGrayColorThunk),
// Coord, Family-record and float-table side effects are stubbed/forwarded.
// ===========================================================================

// gilde.exe 0x46c30c — VIBE_Interaction_PerformRenovate. Always emits "renovieren"
// (action 15). Returns 15.
char PerformRenovate(ContextActor* building, ContextActor* tile, ContextActor* actor);
// gilde.exe 0x46e1ec — VIBE_Interaction_PerformSabotage. Requires event.mode==4 and
// a resolvable building; emits "sabotage" (action 25), returns 23. Else 0.
char PerformSabotage(InteractionEventRec* ev, ContextActor* actor);
// gilde.exe 0x46e7b4 — VIBE_Interaction_PerformBeating. Requires event.mode==7 and a
// resolvable person; emits "pruegel" (action 26), returns 24. Else 0.
char PerformBeating(InteractionEventRec* ev, ContextActor* actor);

} // namespace guild::sim
