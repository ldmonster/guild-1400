#pragma once
// charaction_steps4 — batch 4 of the CharAction step / state-machine LEAVES of the
// Guild simulation (gilde.exe, VIBE_CharAction_* family). These are the remaining
// self-contained per-tick step coroutines that drive a He handler record (see
// he.h), continuing charaction_steps2 / charaction_steps3 / charaction_brawl.
//
// This batch covers the "social / examination / duel" cluster — the heavier,
// event-panel- and message-coupled steps the earlier batches deferred:
//   * the journeyman-recruit completion step (JourneymanRecruitStep) — packet
//     status gated, renders the recruit message and frees;
//   * the buy-object decision step (BuyObjectStep) — scans the actor's home city
//     for a market building, picks a random seller, adjusts its mood, frees;
//   * the wait-then-move negotiation step (WaitThenMoveStep) — a per-tick
//     countdown that, on expiry, rolls a willingness check off the city loyalty
//     byte and emits either an accept or refuse message + cmd15;
//   * the gossip broadcaster (GossipBroadcast) — walks the 268-byte-stride person
//     table, sends a rumor message to eligible persons and a wealth-scaled bribe;
//   * the duel cluster: arm a combatant (DuelArmCombatant), the per-state resolve
//     coroutine (DuelResolveStep), and the dispatcher (DuelDispatch) that drives
//     the event-panel duel dialog through its phases;
//   * the master-exam prompt + decide steps (MasterExamPromptStep,
//     MasterExamDecideStep) — event-panel dialogs gated on the wealth-fee compare,
//     resolved against the dialog-result globals.
//
// The state-machine control flow, the RNG draws, the time deltas, the packet-
// status gates and the exact He-record field offsets are translated 1:1. The
// cross-cluster leaf side effects (person/object resolve, the formatted-message
// renders, the event-panel + form + rich-string UI, the cmd15/cmd16/cmd25/cmd27/
// cmd28/cmd29/cmd39 emits, audio, the office/wealth queries, and the dialog-result
// globals) are routed through the CharActionStep4Hooks bridge below plus the
// shared NpcLeafHooks (npcaction.h). Installing a null hook table restores the
// inert default (every resolve reports absent, every emit/render is a no-op, every
// query returns 0). DuelDispatch / DuelResolveStep reuse DuelIntroMessage from
// charaction_steps3.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// CharActionStep4 cross-cluster leaves. A null member installs an inert default.
// The shared NpcLeafHooks (npcaction.h) still supplies freeHandlerEntry /
// queueRequestEntity29 / packetStatus; this struct adds the person/object
// resolves, the render/UI bridge, the command emits and the dialog-result globals.
// ===========================================================================
struct CharActionStep4Hooks {
    // --- resolves --------------------------------------------------------
    // VIBE_Person_FindRecordById(id) — resolve a person id to a record base; null
    // == absent. (Duel combatant + recruit lookups.)
    HeRecord* (*findPersonById)(i32 id);
    // VIBE_GameObject_ResolveEntityById(..., id, ...) — resolve an entity/object id
    // to a record base; null == absent.
    HeRecord* (*resolveEntityById)(i32 id);
    // VIBE_Person_QueryBegin(buf, a, b, c) — begin a person query; returns the
    // first matching record (or null). The buy-object step iterates it.
    HeRecord* (*personQueryBegin)(int a, int b, int c);
    // VIBE_Person_IterNext() — continue the active person query; null at the end.
    HeRecord* (*personIterNext)();
    // VIBE_He_FindFirstHandlerByFilter / VIBE_He_FindNextMatchingHandler — the
    // live handler-pool scan DuelDispatch uses (selector 1, value = id@+172).
    HeRecord* (*findFirstByFilter)(int sel, int val);
    HeRecord* (*findNextMatching)();

    // --- per-record queries ---------------------------------------------
    // VIBE_Building_MapTypeToCategory(typeByte) — map a building type byte to a
    // category (6 == market). (BuyObjectStep filter.)
    int (*buildingCategory)(u8 typeByte);
    // VIBE_Person_ComputeTotalWealth(cityIndex, rec) — total wealth (currency).
    int (*personWealth)(u16 cityIndex, HeRecord* rec);
    // VIBE_Math_RandomModulo(n) — uniform draw in [0, n).
    int (*randomModulo)(int n);
    // The city loyalty/willingness byte byte_12CE990[536*cityIndex] (WaitThenMove)
    // and the city category byte byte_12CE912[536*cityIndex] (BuyObject) — the
    // hook reads the owning table by city index.
    u8 (*cityWillingness)(u16 cityIndex);   // byte_12CE990[536*idx]
    u8 (*cityCategory)(u16 cityIndex);      // byte_12CE912[536*idx]
    // dword_12CE914[134*cityIndex] — the city's person/entity id (recipient id).
    i32 (*cityPersonId)(u16 cityIndex);

    // --- emits / effects -------------------------------------------------
    // VIBE_Person_AdjustMoodAndNotify(rec, delta) — adjust a person's mood.
    void (*adjustMood)(HeRecord* rec, int delta);
    // VIBE_Command_QueueRequestArgs25(entityId, sel, mask, mode, arg) — cmd25.
    void (*queueArgs25)(i32 entityId, int sel, int mask, int mode, int arg);
    // VIBE_Command_QueueRequestCoord27(toId, fromId, value) — cmd27.
    void (*queueCoord27)(i32 toId, i32 fromId, int value);
    // VIBE_Command_EnqueueCmd15(arg, cityPersonId, value, flagByte) — cmd15.
    void (*enqueueCmd15)(i32 arg, i32 cityPersonId, int value, u8 flagByte);
    // VIBE_Command_QueueRequest16(toId, arg, value, flagByte) — cmd16 bribe.
    void (*queueRequest16)(i32 toId, i32 arg, int value, u8 flagByte);
    // VIBE_He_SendEntityMessage(toId, arg1, arg2, textId, ...) — deliver a built
    // message to an entity. The text itself is render-bridge (opaque here);
    // `textId` is the rendered template id the caller built.
    void (*sendEntityMessage)(i32 toId, int textId);
    // The recruit + buy-object messages additionally go through
    // VIBE_He_SendQuickjumpMessage; collapsed into a quickjump emit (recipient,
    // textId). Opaque side effect.
    void (*sendQuickjumpMessage)(i32 toId, int textId);
    // VIBE_Building_QueueCommandForAll(cityIndex, op) — broadcast a building op.
    void (*buildingQueueAll)(u16 cityIndex, int op);
    // VIBE_Amt_HighlightGuildMembers(personRec, rank) — guild-member highlight.
    void (*highlightGuildMembers)(HeRecord* personRec, int rank);
    // VIBE_Command_QueueRequest39(buf) collapsed: queue a movement request for the
    // resolved pair (escort/move-to). Opaque side effect.
    void (*queueRequest39)();
    // VIBE_Command_QueueRequestSlotReset28(buf, arg) — cmd28 slot reset.
    void (*queueSlotReset28)();

    // --- event-panel / UI bridge ----------------------------------------
    // VIBE_EventPanel_CreateSlot(rec, a, b) — create the dialog slot. Returns the
    // created slot ptr (stored by the caller into +116 by the engine; here the
    // hook updates the record's +116 itself if it wishes). Opaque.
    void (*eventPanelCreate)(HeRecord* rec);
    // VIBE_EventPanel_DestroySlot(rec, a) — destroy the dialog slot.
    void (*eventPanelDestroy)(HeRecord* rec);
    // VIBE_Form_SelectWindow + VIBE_Text_RenderRichString collapsed: render a
    // rich-string line into the dialog window `page`. Opaque side effect.
    void (*renderDialogLine)(HeRecord* rec, int page, int textId);
    // VIBE_Audio_StartVoiceSample(...) — play the duel intro voice. Opaque.
    void (*playVoice)();
    // VIBE_Person_ResolveStatusFlags / VIBE_Hud_BuildPersonCardSimple collapsed:
    // build the duel person card. Opaque.
    void (*buildPersonCard)(HeRecord* combatant);
    // VIBE_Office_GetHolderEntryByCity(personRec, outA, outB) — nonzero if the
    // city has an office holder; writes the holder rank into *rankOut.
    int (*officeHolder)(HeRecord* personRec, int* rankOut);
    // VIBE_Building_EvalProductionRating(rec, n) — production rating (duel arm).
    int (*productionRating)(HeRecord* rec, int n);

    // --- dialog-result globals ------------------------------------------
    // dword_75BF04 — the currently-focused dialog window handle. The exam/duel
    // steps compare it against their own slot's window handle (+116 -> +8).
    i32 (*dialogWindow)();
    // dword_75BF38 — the last dialog result code (-1 == none, 1210 == accept,
    // 1155 == decline).
    i32 (*dialogResult)();
};
void SetCharActionStep4Hooks(const CharActionStep4Hooks* hooks);
const CharActionStep4Hooks& GetCharActionStep4Hooks();

// ===========================================================================
// Recovered dialog-result constants.
// ===========================================================================
constexpr i32 kDialogAccept  = 1210;   // dword_75BF38 == 1210
constexpr i32 kDialogDecline = 1155;   // dword_75BF38 == 1155
constexpr i32 kDialogNone    = -1;

// ===========================================================================
// Additional He / record field accessors (byte-faithful offsets) used here.
//   +116 (+0x74)  : event-panel dialog-slot pointer (dword; null == no slot).
//   +172 (+0xAC)  : counter / source person id / countdown byte (dword).
//   +176 (+0xB0)  : computed fee / target id / wealth (dword).
//   +180 (+0xB4)  : auxiliary id (dword).
//   +184 (+0xB8)  : recruit packet handle (dword).
// ===========================================================================
inline i32&      Cas4_Slot116(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 116); }
inline i32&      Cas4_Src172(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline u8&       Cas4_Countdown(HeRecord* h) { return *reinterpret_cast<u8*>(HeBytes(h) + 172); }
inline i32&      Cas4_Fee176(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&      Cas4_Aux180(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32&      Cas4_Packet184(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }

// ===========================================================================
// Translated step functions.
// ===========================================================================

// gilde.exe 0x4d07b8 — VIBE_CharAction_JourneymanRecruitStep.
//   Stamp the clock into +82 (+4 minutes). Resolve the two entities at +176 and
//   +172. If state@+112 == -2 or the +176 entity is absent, free. If state == 0
//   and the recruit packet (+184) has been applied: render the recruit message,
//   queue a named-object request, deliver the quickjump message, then free.
//   Returns the state (or free's result).
i32 JourneymanRecruitStep(HeRecord* h);

// gilde.exe 0x4d16a4 — VIBE_CharAction_BuyObjectStep.
//   state@+112: -2 -> free; nonzero -> return it; 0 -> scan the actor's home-city
//   persons for a market building (category 6, up to 64), pick a random one,
//   adjust its mood by -50, and (if the city category byte is 6) send the buyer a
//   quickjump message. Then free.
i32 BuyObjectStep(HeRecord* h);

// gilde.exe 0x4d1168 — VIBE_CharAction_WaitThenMoveStep.
//   state@+112: -2 -> free; nonzero -> return it; 0 -> if the +172 countdown byte
//   is nonzero, decrement it and stay; else roll RandomModulo(256) against the
//   city willingness threshold and emit either an accept (>= threshold) or refuse
//   (< threshold) message + cmd15, then free.
i32 WaitThenMoveStep(HeRecord* h);

// gilde.exe 0x4d0b84 — VIBE_CharAction_GossipBroadcast.
//   state@+112 == -2 -> free. Else render the rumor message, walk the 768-entry
//   person table (268-byte stride): for each present, loyalty>=2, eligible-class
//   person, find the nearest target, send the rumor (if class 6/7), and if a
//   target resolved send a wealth-scaled bribe (cmd16) + acceptance message. Then
//   free.
i32 GossipBroadcast(HeRecord* h);

// gilde.exe 0x4cfab4 — VIBE_CharAction_DuelArmCombatant(h@eax, opponent@edx,
//                                                       self@ebx).
//   If the opponent's class byte (+2) is 6 or 7: render the duel-arm message,
//   deliver it, and disarm both combatants (cmd25 456). If self has an office
//   (byte+358), resolve its rank. Compute a production-rated value, highlight the
//   guild members, queue a coord-27 move request, stamp the clock into +82 and
//   queue a cmd29 entity request. Returns the cmd29 handle.
i32 DuelArmCombatant(HeRecord* h, HeRecord* opponent, HeRecord* self);

// gilde.exe 0x4d0438 — VIBE_CharAction_DuelResolveStep.
//   Gated on the +132 packet status. Switch on (state@+112 + 2): cases 0/1 disarm
//   both combatants, re-arm cmd29 (+2 min) if (flags & 2), then free; case 3
//   disarms + re-arms (no free); case 4 additionally queues a move-39 request for
//   the resolved pair before disarming + re-arming. Returns the last result.
i32 DuelResolveStep(HeRecord* h);

// gilde.exe 0x4cfc24 — VIBE_CharAction_DuelDispatch.
//   Find the partner handler (filter 1 == id@+172); resolve both combatants. If
//   the state is terminal or a combatant is missing, tear down the dialog, re-arm
//   the partner's cmd29 and free. state 0 opens the event-panel duel dialog and
//   schedules +2 days (state -> 1). state 1, once the appointment is due and the
//   dialog window is focused, dispatches the result: 1210 -> DuelIntroMessage,
//   1155 -> rearm DuelArmCombatant, both then tear down + free.
i32 DuelDispatch(HeRecord* h);

// gilde.exe 0x4d0d98 — VIBE_CharAction_MasterExamPromptStep.
//   Terminal-state (>= -2) tear-down. Else, once the +68 deadline is due: state 0
//   opens the exam prompt dialog (fee = wealth * factor) and advances to state 1;
//   state 1 dispatches the dialog result (1210 -> charge the fee via cmd15 +
//   building broadcast 8; 1155 -> building broadcast -6), then tears down.
i32 MasterExamPromptStep(HeRecord* h);

// gilde.exe 0x4d0f58 — VIBE_CharAction_MasterExamDecideStep.
//   Terminal-state tear-down. Else, once the +68 deadline is due: state 0 opens
//   the decide dialog (fee = wealth * factor, stored into +176) -> state 1;
//   state 1, once the +82 deadline is due and the result is 1210, charges the fee
//   (cmd16 + cmd28 slot reset) and tears down; 1155 just tears down.
i32 MasterExamDecideStep(HeRecord* h);

} // namespace guild::sim
