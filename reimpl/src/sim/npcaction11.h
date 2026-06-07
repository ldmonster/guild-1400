#pragma once
// NpcAction11 — eleventh batch of the Guild (gilde.exe) NpcAction behaviours.
// This batch finishes the remaining UNTRANSLATED deterministic VIBE_NpcAction_*
// functions: the "Begin*" appointment-stamping launchers, the small per-NPC
// "He"/handler step coroutines driven by the +112 state dword / +120 flag byte
// (DropObject / PickupObject / UseObject / DecrementCarry / DismissStaff /
// BroadcastMoveToTargets / CheckTargetBusy), the ranged-attack AI evaluator
// EvaluateUseBack, the relation-window social launchers BeginFriendship /
// BeginDivorce, and the office-action emitters GrantAiCredit / BuildWell.
//
// Translated 1:1 against the Hex-Rays pseudocode (disasm-resolved where the
// decompiler emitted uninitialised-local artifacts):
//
//   0x4e4728 BeginEquipObject          — stamp +68/+82 clock, Advance by the
//                                        person's apprentice-span hours (or 0/24h
//                                        fallback / dword_63C7B8 fast path), set
//                                        the +90 equip bit, emit cmd-arg 25.
//   0x4e4ed8 BeginUnequipObject        — find building, stamp +82, Advance by
//                                        RandomModulo(10)+10 minutes, emit arg25.
//   0x4e4a48 BeginUseObject            — stamp +68/+82, Advance 5d / 1h-fast,
//                                        query person, set +16 obj, arg25 if +90>=0.
//   0x4e4c84 BeginStoreObject          — query person, arg25(1024), stamp +82.
//   0x4e5b20 DecrementCarryStep        — state machine: decrement +184; on <0
//                                        slot-reset 28 to host kinds + buildop72.
//   0x4ea10c CheckTargetBusyState      — scan handlers (filter 60) for a conflict
//                                        on +180/+172, abort (+112=-1) on busy
//                                        target or rank, then Advance +1s.
//   0x4e6d2c DismissStaffStep          — dismiss staff: clear +364 delta field,
//                                        recall coord, buildop71/77, named-obj 53.
//   0x4e73c0 BroadcastMoveToTargetsStep— rally: per-city coord27 broadcast scaled
//                                        by +172/money-rate, then host quickjump.
//   0x4e4834 DropObjectStep            — drop carried object: host/peer quickjump
//                                        messages, single58 + arg25, free.
//   0x4e454c PickupObjectStep          — timed pickup: mood adjust by elapsed*rate,
//                                        re-arm (+10m) while +176>0 else finish.
//   0x4e4af4 UseObjectStep             — 4-phase use: arg25 setup, request17 use,
//                                        host quickjump, free.
//   0x471b10 EvaluateUseBack           — ranged-attack AI: gun-cooldown gate, then
//                                        SelectBestRecursive(40) or TryRangedAttack.
//   0x568650 BeginFriendship           — open relation-overview window (host) or
//                                        resolve target, coord27(+25), notify.
//   0x5692dc BeginDivorce              — two relation-overview picks (host) or
//                                        resolve spouses, coord27(-25) both, notify.
//   0x473e00 GrantAiCredit             — AI credit: resolve giver/object, building
//                                        action start, cmd15 transfer, delta field.
//   0x473448 BuildWell                 — build/upgrade a well: office-storage gate,
//                                        building action start, slot-worth cmd15.
//
// Every +112/+120 transition, the GameTime stamping (the 14-byte clock image into
// +68/+82/+196), each Advance delta, each RandomModulo draw, and every command
// emission is 1:1 with the disassembly. Cross-cluster leaves (Person/Building/
// GameObject queries, the grid record arrays word_12CE910/dword_12CE914, the
// command builders, the relation-window/text/voice broadcasts, the AI evaluators)
// are routed through NpcAction11Hooks with inert defaults, mirroring the
// NpcAction10Hooks pattern so the machines run in isolation against a synthetic
// scene. The shared global game clock is the existing NpcClock() (npcaction.cpp).
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants (resolved byte-for-byte via get_bytes).
// ===========================================================================
extern const double kPickupMoodMul;   // dbl_61F650 = elapsed-minute mood multiplier
extern const double kPickupMoodBias;  // dbl_61F658 = mood additive bias
extern const float  kWellWorthMul;    // flt_61A5F4 = upgrade slot-worth cost factor

// ===========================================================================
// Leaf hooks. nullptr installs an inert default (every effect a no-op, every query
// returns absent/zero). A `void*` is the opaque Person/Building/GameObject record
// handle the originals keep in a register; accessors below read its fields:
//   objId(rec)   = *(rec+1)   entity id
//   markerWord   = *(rec+0)   city/person word index
//   kind(rec)    = *(rec+2)   the 5/6/7 NPC/host/player gate byte
//   rank(rec)    = *(rec+433) busy/rank byte
// ===========================================================================
struct NpcAction11Hooks {
    // --- handler lifecycle / scanning ---
    i32  (*freeHandlerEntry)(HeRecord* h);             // He_FreeHandlerEntry
    // He_FindFirstHandlerByFilter(1,0,filter) then FindNext loop: returns the first
    // OTHER handler whose +180 (45*4) matches `target180` or +172 (43*4) matches
    // `target172`, else nullptr. (CheckTargetBusyState's busy-conflict probe.)
    void* (*findConflictingHandler)(HeRecord* self, int filter,
                                    i32 target180, i32 target172);

    // --- entity record lookup / queries ---
    void* (*findPersonById)(i32 id);                   // Person_FindRecordById
    void* (*personQueryBegin)(i32 filterId);           // Person_QueryBegin(...,filterId)
    void* (*buildingFindById)(i32 id);                 // Building_FindById
    void* (*buildingFindOfficeStorage)(int kind, HeRecord* h); // Building_FindOfficeStorage
    void  (*resolveEntity)(i32 id, void** out);        // GameObject_ResolveEntityById
    // GameObject_QueryFind(base, a,b,c[,d]) -> opaque (the multi-arg object probe).
    void* (*gameObjectQueryFind)(i32 base, int a, int b, int c, int d);

    // --- record field reads ---
    i32  (*objId)(void* rec);                          // *(rec+1)
    u16  (*markerWord)(void* rec);                      // *(rec+0)
    u8   (*kind)(void* rec);                            // *(rec+2)
    u8   (*rank)(void* rec);                            // *(rec+433)
    u8   (*equipFlags)(void* rec);                      // *(rec+90)
    void (*setEquipFlags)(void* rec, u8 v);             // *(rec+90)=
    i32  (*field101)(void* rec);                        // *(rec+101) carry/busy id
    i32  (*field364)(void* rec);                        // *(rec+364) staff-link id
    i32  (*familyDur)(void* rec, int which);            // apprentice span hours raw
    i32  (*spouseId)(void* rec, int which);             // relation arg dword (+4/+8)

    // --- grid record arrays (per-city) ---
    i32  (*cityId)(u16 cityIndex);                     // dword_12CE914[134*idx]
    u16  (*cityMarker)(u16 cityIndex);                 // word_12CE910[268*idx]
    u8   (*cityKind)(u16 cityIndex);                   // byte_12CE912[536*idx]
    void* (*cityPersonRecord)(u16 cityIndex);          // &word_12CE910[268*idx]

    // --- command builders (all emit-only, no return used) ---
    void (*requestArgs25)(i32 id, int a, int b, int c, int d);   // QueueRequestArgs25
    void (*request17)(i32 idA, i32 idB, int c, int d, u8 cur, int f); // QueueRequest17
    void (*requestSingle58)(i32 id);                   // QueueRequestSingle58
    void (*requestCoord27)(i32 a, i32 b, int delta);   // QueueRequestCoord27
    void (*requestSlotReset28)(void* slotImage);       // QueueRequestSlotReset28
    void (*requestBuildOp71)(i32 id, int a, int b, int c);
    void (*requestBuildOp72)(i32 id, int amount);
    void (*requestBuildOp77)(i32 id);
    void (*requestNamedObject53)(i32 id, i32 objId, i32 target, int flag, const char* name);
    void (*requestState23)();                          // QueueRequestState23
    void (*beginDelta)(void* rec, i32 id);             // BeginDeltaPacket
    void (*appendCopiedField)(void* rec, int field);   // AppendCopiedField(4,1,...)
    void (*appendRawField)(void* rec, int field, i32 value);     // AppendRawField
    void (*requestState22)();                          // QueueRequestState22
    // BuildWell / GrantAiCredit office-action bracket + transfer:
    void (*buildingActionStart)(const char* name);     // EnqueueBuildingActionStart
    void (*buildingActionEnd)();                       // EnqueueBuildingActionEnd
    void (*enqueueCmd15)(i32 idA, i32 idB, i32 amount, u8 cur);   // EnqueueCmd15
    i32  (*buildingSumFlaggedSlotsWorth)(int group);   // Building_SumFlaggedSlotsWorth
    bool (*aiLoadBuildingGraphic)(void* objA, void* objB);       // AiAction_LoadBuildingGraphic

    // --- text / voice / history / relation ---
    void (*sendEntity)(i32 targetId, int textId);      // Text_Render + He_SendEntityMessage
    void (*sendQuickjump)(i32 cityId, int textId, i32 objId, const char* tag); // SendQuickjumpMessage
    // Amt_RunOfficeOverviewWindow(prompt,...) -> opaque person rec or nullptr.
    void* (*runOfficeOverviewWindow)(int mode);
    i32  (*relationLookup)(void* a, void* b);          // Relation_LookupMatrixEntry
    void (*adjustMood)(void* rec, i32 delta);          // Person_AdjustMoodAndNotify
    void (*evaluateViolation)(int crime, i32 a, i32 b, i32 c, i32 d); // Gesetz_EvaluateViolation
    i32  (*moneyMultiplyByRate)(i32 amount, u8 cur);   // Money_MultiplyByRate
    // EvaluateUseBack AI leaves:
    u16  (*randomModulo)(u16 n);                       // Math_RandomModulo
    u8   (*selectBestRecursive)(int method, u16 self, void* a, void* b); // AiMethod_SelectBestRecursive
    bool (*tryRangedAttack)(void* self, void* out);    // AiPlayer_TryRangedAttack
    u8   (*personEquipState)(void* rec, int idx);      // *(rec+485) gun-cooldown bits
    i32  (*field92)(void* rec);                         // *(rec+92) held-object link
    // dword_63C7B8 — the global "fast/debug-speed" gate. When true the Begin*
    // launchers take the instant (0-day) appointment branch. Inert default false.
    bool (*fastMode)();                                 // dword_63C7B8 != 0
};

void SetNpcAction11Hooks(const NpcAction11Hooks* hooks);
const NpcAction11Hooks& GetNpcAction11Hooks();

// ===========================================================================
// Translated functions. Each returns the original al/eax where the caller uses it.
// ===========================================================================

// gilde.exe 0x4e4728 — VIBE_NpcAction_BeginEquipObject(h@eax). Orig eax.
i32 NpcAction11_BeginEquipObject(HeRecord* h);

// gilde.exe 0x4e4ed8 — VIBE_NpcAction_BeginUnequipObject(h@eax). Orig eax.
i32 NpcAction11_BeginUnequipObject(HeRecord* h);

// gilde.exe 0x4e4a48 — VIBE_NpcAction_BeginUseObject(h@eax). Orig eax (record/cmd ret).
void* NpcAction11_BeginUseObject(HeRecord* h);

// gilde.exe 0x4e4c84 — VIBE_NpcAction_BeginStoreObject(h@eax). Orig eax.
void* NpcAction11_BeginStoreObject(HeRecord* h);

// gilde.exe 0x4e5b20 — VIBE_NpcAction_DecrementCarryStep(h@eax). Orig eax.
i32 NpcAction11_DecrementCarryStep(HeRecord* h);

// gilde.exe 0x4ea10c — VIBE_NpcAction_CheckTargetBusyState(h@esi).
void NpcAction11_CheckTargetBusyState(HeRecord* h);

// gilde.exe 0x4e6d2c — VIBE_NpcAction_DismissStaffStep(h@eax). Orig eax.
i32 NpcAction11_DismissStaffStep(HeRecord* h);

// gilde.exe 0x4e73c0 — VIBE_NpcAction_BroadcastMoveToTargetsStep(h@eax). Orig eax.
i32 NpcAction11_BroadcastMoveToTargetsStep(HeRecord* h);

// gilde.exe 0x4e4834 — VIBE_NpcAction_DropObjectStep(h@eax). Orig eax.
i32 NpcAction11_DropObjectStep(HeRecord* h);

// gilde.exe 0x4e454c — VIBE_NpcAction_PickupObjectStep(h@eax).
void NpcAction11_PickupObjectStep(HeRecord* h);

// gilde.exe 0x4e4af4 — VIBE_NpcAction_UseObjectStep(h@eax). Orig eax.
void* NpcAction11_UseObjectStep(HeRecord* h);

// gilde.exe 0x471b10 — VIBE_NpcAction_EvaluateUseBack(person@edx, outA@ecx,
//   relFlag@bl, outB@esp). Returns 0 / 42. On success writes outA/outB (24 bytes).
u8 NpcAction11_EvaluateUseBack(void* person, void* outA, u8 relFlag, void* outB);

// gilde.exe 0x568650 — VIBE_NpcAction_BeginFriendship(self@eax, relCtx@edx). Orig eax.
i32 NpcAction11_BeginFriendship(void* self, void* relCtx);

// gilde.exe 0x5692dc — VIBE_NpcAction_BeginDivorce(self@eax, relCtx@edx). Orig eax.
i32 NpcAction11_BeginDivorce(void* self, void* relCtx);

// gilde.exe 0x473e00 — VIBE_NpcAction_GrantAiCredit(self@eax, giver@edx, obj@ebx).
//   Returns 0 always (the original al is 0 on every path).
u8 NpcAction11_GrantAiCredit(HeRecord* self, void* giver, void* obj);

// gilde.exe 0x473448 — VIBE_NpcAction_BuildWell(h@eax, building@edx, action@ebx).
//   Returns 0 (gate/fail) or 47 (queued).
u8 NpcAction11_BuildWell(HeRecord* h, void* building, u8* action);

} // namespace guild::sim
