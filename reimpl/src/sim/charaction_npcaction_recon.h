#pragma once
// charaction_npcaction_recon — additional CharAction / NpcAction per-action step
// routines recovered from gilde.exe (the VIBE_CharAction_* / VIBE_NpcAction_*
// action-step family). These are the *bodies* the batch-6 handler-registration
// table (charaction_steps6.cpp, kCharActionHandlerTable) only listed by address;
// this module fills in the clean, control-flow-faithful integer state machines.
//
// SCOPE / FAITHFULNESS NOTE (project rule 8 — no cheap analogues):
//   Only the routines whose Hex-Rays decompilation is an UNAMBIGUOUS integer
//   state machine are translated here, 1:1. Several siblings in the same cluster
//   (DuelInit 0x4cfed8, PickpocketStep 0x4d3188, SellObjectStep 0x4d13b4,
//   GoToTavernStep 0x4d2314, DrinkInit 0x4d21ec, RunOfficeGuardAssign 0x4db2e4,
//   ProtectionMoneyStep 0x4d3918, PatrolInit 0x4cdd38, ArrestInit 0x4cf544,
//   FindNearbyPeerState 0x4cae78, ApproachTargetState 0x4cb5dc,
//   GreetTargetState 0x4cb74c) decompile with lost x87/FPU call arguments
//   (VIBE_Coord_ConvertX() with an empty argument list), UI/global-state coupling
//   (EventPanel/Form/dword_75BFxx) or unrecovered __usercall register aliasing
//   (a second VIBE_Person_FindRecordById whose result the decompiler dropped into
//   an undefined edx/ecx). Translating those would require GUESSING the dropped
//   operands, which rule 8 forbids — they are deliberately OMITTED and reported.
//
// Every cross-cluster leaf (person/object resolve, query iteration, the law
// evaluation, the formatted-message renders, the cmdNN emits, the handler-pool
// scan, freeHandlerEntry / queueRequestEntity29 / packetStatus) is routed through
// the CharActionReconHooks bridge below; installing a null table restores the
// inert default (every resolve reports absent, every emit a no-op, every query 0)
// so the control flow is exercised headlessly. The 14-byte global clock image
// (qword_13CE852) is supplied by NpcClock(); GameTimeAdvance / GameTimeCompare
// are reused. He-record field offsets are byte-faithful (he.h accessors).
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Extra byte-faithful He / record offsets used by these routines. Additive to
// the he.h map; addressed explicitly (mirrors `*(T*)(base+off)`).
//   +0x56 (+86)   : action-type word the *Init routines stamp (e.g. 7 / 20 / 8).
//   +0x58 (+88)   : init scratch dword (cleared to 0; DuelInit/Candidacy use 30).
//   +0xAC (+172)  : primary target id (dword)  / DrinkInit member-count byte.
//   +0xB0 (+176)  : secondary target id (dword).
//   +0xB4 (+180)  : tertiary id / scan-step (dword).
//   +0xB8 (+184)  : seq/counter id (dword)     / GuildJoin retry byte (+184).
//   +0xC0 (+192)  : duel/scan stride word; BeginActionState20 keys on ==1.
//   +0xC4 (+196..)  : NpcAction second clock snapshot (BeginCarryGoods +196..209).
//   +0xC8 (+200)  : second-clock action word (BeginCarryGoods stamps 8).
//   +0xD4 (+212)  : quad60 request handle (FindNearbyPeer / state machines).
//   +57.. (byte at +193>>24): packed sub-method index.
// ---------------------------------------------------------------------------
inline u16&  HeR_TypeWord(HeRecord* h)    { return *reinterpret_cast<u16*>(HeBytes(h) + 86); }
inline i32&  HeR_InitScratch(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 88); }
inline i32&  HeR_Target(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32&  HeR_Target2(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&  HeR_Target3(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32&  HeR_SeqId(HeRecord* h)       { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline u8&   HeR_RetryByte(HeRecord* h)   { return *reinterpret_cast<u8*>(HeBytes(h) + 184); }
inline i32&  HeR_StrideWord(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 192); }
inline GameTime& HeR_Clock2(HeRecord* h)  { return *reinterpret_cast<GameTime*>(HeBytes(h) + 196); }
inline u16&  HeR_Clock2Word(HeRecord* h)  { return *reinterpret_cast<u16*>(HeBytes(h) + 200); }
inline i32&  HeR_QuadHandle(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 212); }
inline i32&  HeR_CarryArg(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 188); }
// Packed sub-method dword at +169 (the >>24 byte the *Init/Step routines key off).
inline i32&  HeR_Packed169(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 169); }
inline i32&  HeR_Packed173(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 173); }

// ---------------------------------------------------------------------------
// Cross-cluster leaf bridge. A null member installs the inert default.
// ---------------------------------------------------------------------------
struct CharActionReconHooks {
    // --- resolves / queries ---------------------------------------------
    // VIBE_GameObject_ResolveEntityById(out, 0, id, 0) — write resolved base / null.
    void (*resolveEntityById)(HeRecord** out, i32 id);
    // VIBE_GameObject_QueryFind(scene, a, b, key) — nonzero record / null.
    HeRecord* (*objectQueryFind)(i32 scene, int a, int b, i32 key);
    // VIBE_Person_FindRecordById(id) — resolve a person id to its record / null.
    HeRecord* (*findPersonById)(i32 id);
    // VIBE_Person_QueryBegin(self, a, b, key) — begin a person query; first match.
    HeRecord* (*personQueryBegin)(i32 self, int a, int b, i32 key);
    // VIBE_He_FindFirstHandlerByFilter(a, b, filter) / FindNextMatchingHandler.
    HeRecord* (*findFirstByFilter)(int a, int b, i32 filter);
    HeRecord* (*findNextMatching)();
    // VIBE_Command_GetPacketStatusById(handle) — nonzero once applied; 0 pending.
    i32 (*packetStatus)(i32 handle);

    // --- emits / effects -------------------------------------------------
    // VIBE_Command_QueueRequestEntity29(arg, record) — cmd29; returns handle.
    i32 (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_He_FreeHandlerEntry(record) — release the handler entry; result code.
    i32 (*freeHandlerEntry)(HeRecord* h);
    // VIBE_Command_QueueRequestMixed45(id, lo, hi) — the BeginActionState7 emit.
    void (*queueRequestMixed45)(i32 id, i32 lo, i32 hi);
    // VIBE_Command_QueueRequestQuad60(a, b, c, d) — FindNearbyPeer / cancel emit.
    i32 (*queueRequestQuad60)(i32 a, i32 b, int c, i32 d);
    // VIBE_Command_QueueRequestArgs25(id, off, val, sz, extra) — ArrestStep emit.
    void (*queueRequestArgs25)(i32 id, int off, int val, int sz, int extra);
    // VIBE_Command_QueueRequestCoord27(from, to, delta) — coordinate request.
    void (*queueRequestCoord27)(i32 from, i32 to, int delta);
    // VIBE_He_SendEntityMessage(toId, textId) — deliver a rendered message.
    void (*sendEntityMessage)(i32 toId, int textId);
    // VIBE_He_SendQuickjumpMessage(toId, textId) — quickjump message.
    void (*sendQuickjumpMessage)(i32 toId, int textId);
    // VIBE_Gesetz_EvaluateViolation(kind, sev, victim, recipient, obj) — handle.
    i32 (*evaluateViolation)(int kind, int sev, i32 victim, i32 recipient, i32 obj);
    // VIBE_NpcAction_StampTimeAndRequestEntity(record) — the cancel leaf (0x4c9458).
    void (*stampTimeAndRequest)(HeRecord* h);

    // --- GuildJoinStep packet builders ----------------------------------
    void (*beginDeltaPacket)(i32 a, i32 b);
    void (*appendRawField)(i32 a, i32 b, const u8* buf, int v);
    void (*queueRequestState22)();
    void (*requestBuildOp72)(i32 id, int sub);
    u8   (*buildingGroupFromCode)(int code);
    void (*buildingGuildRankPair)(int group, u8* outA, u8* outB);
    i32  (*meisterRegisterApEvent)(u16 person, i32 amount, int z);

    // --- RunOfficeCandidacy leaves --------------------------------------
    i32  (*packetSeqById)(i32 handle);
    void (*officeConfirmCandidacy)(i32 seq, u8 cat);
    u8   (*officeFindHighestVacantRank)(u8 cat);
    i32  (*officeRenderRequirementText)(u8 rank);

    // VIBE_Math_RandomModulo(n) — uniform draw in [0, n). Inert default 0.
    int (*randomModulo)(int n);
};
void SetCharActionReconHooks(const CharActionReconHooks* hooks);
const CharActionReconHooks& GetCharActionReconHooks();

// ===========================================================================
// Translated routines.
// ===========================================================================

// gilde.exe 0x4ca938 — VIBE_NpcAction_BeginActionState7(h@eax).
//   If the spawn flag (+120 & 4) is set, return unchanged. Else stamp +82 (+24h),
//   set action word +86=7, clear +88, resolve the actor entity (+16). If absent,
//   arm a -1 cmd29; if present, when no "437" object is co-located issue the
//   mixed45 unbind, then reset +132 and arm a 0 cmd29. Returns the cmd29 handle
//   (or the original record value when the spawn flag short-circuits).
i32 NpcReconBeginActionState7(HeRecord* h);

// gilde.exe 0x4cbc20 — VIBE_NpcAction_BeginActionState20(h@eax).
//   Clear +112. If spawn flag set, return unchanged. Else stamp +82 (+24h), set
//   +86=20, clear +88; when the stride word (+192)==1, walk the 4-slot id array
//   (+16..+28) and, for each resolvable person, emit a slot-reset (28) carrying a
//   priority byte (9 for category 6/7, else 1). Arm a 0 cmd29. Returns the handle.
i32 NpcReconBeginActionState20(HeRecord* h);

// gilde.exe 0x4cf798 — VIBE_CharAction_ArrestStep(h@eax,edi,esi).
//   State machine on +112. States < -1 (only -2) and -1 release the two arrest
//   flags (args25 off 456, masks 256/512) and free the entry. State 2 releases the
//   flags, drags the suspect toward the officer (coord27 -104), renders the two
//   arrest messages for category 6/7 actors, stamps +82 (+2 min) and arms a -1
//   cmd29. Other states pass through. Returns the per-state result.
i32 CharReconArrestStep(HeRecord* h);

// gilde.exe 0x4d1d40 — VIBE_CharAction_GuildJoinStep(h@eax,edi,esi).
//   State -2/-1 frees the entry. Gate on the spawn flag and the entity packet
//   (+132). State 0: render the join intro, bump the retry byte (+184) and the
//   action word (+86 += 24), arm a cmd29 (arg = retry>=3), register the AP event
//   (-(+180) AP) and return. State 1: resolve the building group, build the two
//   random delta fields, emit op72, render the welcome/decline message, stamp +82
//   and arm a -1 cmd29. Returns the per-state result.
i32 CharReconGuildJoinStep(HeRecord* h);

// NOTE: VIBE_CharAction_RunOfficeCandidacy (0x4db624) is OMITTED (rule 8): its
// state-1 decompile reaches the arm-decision with an uninitialized branch variable
// on the packet-confirm path (Hex-Rays drops the init), so the control flow cannot
// be recovered faithfully. See the .cpp for the full rationale.

// gilde.exe 0x4dc074 — VIBE_CharAction_CancelEntityActions(record@eax).
//   Scan the handler pool seven times (filters 65, 111, 69, 44, 71, 94, 95) for
//   handlers whose actor/peer id (+43*4 / +44*4) matches the cancelled person's id
//   (+4). For the matching active handlers (flag +120 & 3) it resolves the peer,
//   re-stamps the appointment (StampTimeAndRequestEntity) and, for filter 44, also
//   emits a quad60 release. Returns the last scan cursor (null). The person id is
//   read from record+4; a null/0xFFFF record is a no-op.
HeRecord* CharReconCancelEntityActions(HeRecord* record);

// gilde.exe 0x4e55bc — VIBE_NpcAction_BeginCarryGoods(h@eax).
//   Stamp +82 and the second clock (+196..209); advance the second clock by
//   24*(+184) hours; set its action word +200=8 and mirror +184 -> +188. Resolve
//   the carrier (person query by +172). If present and the report flag (+120 & 2)
//   is set and the carrier's city category is 6/7, render the smuggling report
//   (5350), send the quickjump and evaluate the law violation (kind 13). Returns
//   the violation handle (or the carrier record when the report path is skipped).
HeRecord* NpcReconBeginCarryGoods(HeRecord* h);

} // namespace guild::sim
