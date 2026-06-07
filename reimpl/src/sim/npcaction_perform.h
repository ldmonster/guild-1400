#pragma once
// NpcActionPerform — the small "intrigue / office" NpcAction *perform* handlers of
// the Guild simulation (gilde.exe, VIBE_NpcAction_Perform* family). Unlike the big
// He-record step coroutines (npcaction2..5 / npcevent_steps*), these are the leaf
// dispatch handlers the interaction/context evaluator calls when an NPC commits to
// a chosen action: each gates on the interaction *type byte* of an action/context
// record, then either
//   * builds a command (op28 slot-reset, op25 args, op72/op90 building ops) wrapped
//     in an EnqueueBuildingAction begin/end pair, or
//   * delegates to an AI sub-behaviour (threaten / slander / spy / dark-corner /
//     tavern-graphic) leaf, or
//   * opens an "Amt" (guild-office) dialog (office menu / election / office-action),
// and returns the NpcAction *result code* (37/40/42/43/44/50/52/54/55/56, or 0 on a
// type mismatch / rejected sub-behaviour). The reject path returns the interaction
// evaluator's "stub" code.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x470ea4 VIBE_NpcAction_PerformSpionage        — "spionage" building action (37)
//   0x471840 VIBE_NpcAction_PerformEnterBuilding   — threaten gate -> 40 / reject
//   0x471cb4 VIBE_NpcAction_PerformUseBack         — slander gate -> op25 + 42
//   0x471dfc VIBE_NpcAction_PerformOpenDoorLarge   — slander-label gate -> 43 / reject
//   0x471f24 VIBE_NpcAction_PerformOpenDoorSmall   — slander-label gate -> 44 / reject
//   0x4737cc VIBE_NpcAction_PerformShopTransaction — spy/dark-corner type pair -> 50
//   0x4742ec VIBE_NpcAction_PerformDrinkTavern     — tavern-graphic gate -> ops + 52
//   0x474c18 VIBE_NpcAction_PerformVerdict         — type 21 -> office menu + 54
//   0x474da0 VIBE_NpcAction_PerformArrest          — type 4  -> election dialog + 55
//   0x4756c4 VIBE_NpcAction_PerformPickupCarry     — type 7/22 -> office action + 56
//
// The action/context records these read are NOT the He record — they are the small
// interaction structs the evaluator passes in registers (eax/edx/ebx). We model
// each as a raw byte pointer and address it by the exact original offsets:
//   +0x00 : interaction type byte        (the dispatch gate)
//   +0x04 : actor/object id (dword)      (passed to the command/dialog leaves)
//   +0x10 : secondary id / sub-type (dword; PickupCarry reads its low byte)
//   +0x0D : packed dword whose HIBYTE (== byte +0x10) DrinkTavern forwards to op72
//   +0x170(+368): live-actor sub-record ptr (Spionage owner-id source; -1 if null)
// Cross-cluster leaves (the Command_* emitters, the AiPlayer_/AiIntrigue_/AiAction_
// sub-behaviours, the Amt_Build* dialog builders, the interaction reject stub) are
// routed through NpcActionPerformHooks so each handler is exercisable in isolation.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Leaf hooks. A null member installs an inert default: the AI/eval gate leaves
// report "rejected" (0), the command/dialog emitters are no-ops, and the reject
// stub returns 0. The action record is passed through opaque (a raw byte pointer).
// ===========================================================================
struct NpcActionPerformHooks {
    // --- command emitters (Spionage / UseBack / DrinkTavern) ---
    // VIBE_Command_EnqueueBuildingActionStart(label) / ...End() — begin/end the
    // building-action batch wrapping the op28 slot-reset.
    void (*enqueueBuildingActionStart)(const char* label);
    void (*enqueueBuildingActionEnd)();
    // VIBE_Command_QueueRequestSlotReset28(&packet) — op28 (the packet is a stack
    // struct the originals fill in; we pass the assembled fields).
    void (*queueRequestSlotReset28)(int actionId, i32 objectId, int seq,
                                    i32 ownerId, i32 fieldA, i32 fieldB);
    // VIBE_Command_QueueRequestArgs25(id, a, b, c, d) — op25.
    void (*queueRequestArgs25)(i32 id, int a, int b, int c, int d);
    // VIBE_Command_RequestBuildOp72(id, byte) — op72 (DrinkTavern).
    void (*requestBuildOp72)(i32 id, u8 value);
    // VIBE_Command_RequestBuildOp90_Thunk(kind, id) — op90 (DrinkTavern).
    void (*requestBuildOp90)(int kind, i32 id);

    // --- AI sub-behaviour gates (return nonzero = accepted) ---
    int (*aiExecThreaten)();                 // VIBE_AiPlayer_ExecThreaten
    int (*aiExecSlander)(i32 ctxId);         // VIBE_AiPlayer_ExecSlander(eax)
    int (*aiFormatSlanderLabel)();           // VIBE_AiIntrigue_FormatSlanderLabel
    int (*aiQueueSpyMission)();              // VIBE_AiPlayer_QueueSpyMission
    int (*aiBuyDarkCorner)();                // VIBE_AiPlayer_BuyDarkCorner
    // VIBE_AiAction_LoadBuildingGraphic(actor, ctx, sub) — nonzero on a resolved
    // graphic (the gate for DrinkTavern's command burst).
    int (*aiLoadBuildingGraphic)(i32 actorId, const void* ctx, i32 sub);

    // --- "Amt" (guild-office) dialog builders ---
    // VIBE_Amt_BuildGuildOfficeMenu(target, &packet)  (Verdict)
    void (*amtBuildGuildOfficeMenu)(i32 target, int kind, i32 id, i32 field16);
    // VIBE_Amt_BuildElectionDialog(target, &packet)   (Arrest)
    void (*amtBuildElectionDialog)(i32 target, int kind, i32 id, i32 field16);
    // VIBE_Amt_BuildOfficeActionDialog(target, &packet) (PickupCarry)
    void (*amtBuildOfficeActionDialog)(i32 target, int kind, i32 id, int seq);

    // --- interaction reject stub (the "not accepted" return code) ---
    int (*interactionEvalRejectStub)();      // VIBE_Interaction_EvalRejectStub
};

void SetNpcActionPerformHooks(const NpcActionPerformHooks* hooks);
const NpcActionPerformHooks& GetNpcActionPerformHooks();

// ===========================================================================
// Perform handlers. Each takes the action/context record(s) the original received
// in registers (modeled as raw byte pointers) and returns the NpcAction result
// code. `actor` is the eax/ecx "owner" action record where one is read; `ctx` is
// the edx/ebx interaction record whose type byte gates the handler.
// ===========================================================================

// gilde.exe 0x470ea4 — VIBE_NpcAction_PerformSpionage(actor@ecx, ctx@edx, live@eax).
//   Requires ctx type == 7. Assembles the op28 packet (actionId 24, objectId =
//   *(live+4), seq -1, byte 2, ownerId = live+368 ? *(that+1) : -1, fields = ctx+1
//   / ctx+4) wrapped in EnqueueBuildingActionStart("spionage")/End. Returns 37.
//   (`actor` is stored to a local in the original but unused by the packet.)
int NpcActionPerform_Spionage(const void* actor, const u8* ctx, const u8* live);

// gilde.exe 0x471840 — VIBE_NpcAction_PerformEnterBuilding().
//   If the threaten sub-behaviour accepts, return 40; else the reject stub.
int NpcActionPerform_EnterBuilding();

// gilde.exe 0x471cb4 — VIBE_NpcAction_PerformUseBack(ctx@eax).
//   If the slander sub-behaviour rejects, return 0; else emit op25(*(ctx+4),
//   484, 512, 4, 0) and return 42.
int NpcActionPerform_UseBack(const u8* ctx);

// gilde.exe 0x471dfc — VIBE_NpcAction_PerformOpenDoorLarge().
//   slander-label gate -> 43, else reject stub.
int NpcActionPerform_OpenDoorLarge();

// gilde.exe 0x471f24 — VIBE_NpcAction_PerformOpenDoorSmall().
//   slander-label gate -> 44, else reject stub. (Same body as OpenDoorLarge with a
//   different success code.)
int NpcActionPerform_OpenDoorSmall();

// gilde.exe 0x4737cc — VIBE_NpcAction_PerformShopTransaction(a@edx, b@ebx).
//   Two type pairs: (*a==4 && *b==1) -> spy-mission gate -> 50/0;
//   (*a==19 && *b==4) -> dark-corner gate -> 50; otherwise 0.
int NpcActionPerform_ShopTransaction(const u8* a, const u8* b);

// gilde.exe 0x4742ec — VIBE_NpcAction_PerformDrinkTavern(actor@eax, ctx@edx, sub@ebx).
//   If the tavern-graphic gate rejects, 0. Else emit op25(*(actor+4), 456,
//   0x1000000, 4, 0), op72(*(actor+4), byte ctx+16), op90(8, *(actor+4)); return 52.
int NpcActionPerform_DrinkTavern(const u8* actor, const u8* ctx, i32 sub);

// gilde.exe 0x474c18 — VIBE_NpcAction_PerformVerdict(actor@ecx, ctx@edx, target@eax).
//   Requires ctx type == 21. Builds the guild-office menu (kind 3, id *(ctx+4),
//   field *(ctx+16)) for `target`; returns 54.
int NpcActionPerform_Verdict(const void* actor, const u8* ctx, i32 target);

// gilde.exe 0x474da0 — VIBE_NpcAction_PerformArrest(actor@ecx, ctx@edx, target@eax).
//   Requires ctx type == 4. Builds the election dialog (kind 2, id *(ctx+4),
//   field *(ctx+16)); returns 55.
int NpcActionPerform_Arrest(const void* actor, const u8* ctx, i32 target);

// gilde.exe 0x4756c4 — VIBE_NpcAction_PerformPickupCarry(actor@ecx, ctx@edx, target@eax).
//   Requires ctx type == 7 or 22. Builds the office-action dialog (kind = byte
//   *(ctx+16), id *(ctx+4), seq 0); returns 56.
int NpcActionPerform_PickupCarry(const void* actor, const u8* ctx, i32 target);

// ===========================================================================
// Registration (address-keyed table, same form as RegisterNpcActions5()).
// ===========================================================================
int RegisterNpcActionsPerform();
const void* NpcActionPerform_TableEntry(int address);

} // namespace guild::sim
