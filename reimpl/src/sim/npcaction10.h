#pragma once
// NpcAction10 — tenth batch of the Guild (gilde.exe) NpcAction behaviour
// state-machine cores. Each is a per-NPC "He"/handler-record coroutine driven by
// the +112 state dword and the +120 flag byte, exactly like the BurglaryStep /
// JailCellStep / RecruitmentState machines reconstructed in npcaction3.cpp. This
// batch translates twelve of the largest remaining UNTRANSLATED VIBE_NpcAction_*
// functions 1:1 against the Hex-Rays pseudocode (disasm-resolved where the
// decompiler left uninitialised-local artifacts):
//
//   0x4e5754 RunCreditStep                  — debt-collection coroutine (+132 packet
//                                             gate; state 0 charges/repossesses, the
//                                             +188 retry counter forks success/fail).
//   0x4cb880 MasterExamState                — master-exam panel coroutine (event-panel
//                                             slot, 24h timer, form-event resolution,
//                                             rating-curve pass/fail roll).
//   0x4eab30 KidnapCarryStep                — kidnap/ransom-carry coroutine (states
//                                             -2/-1 clear flag, 0 query+ransom math).
//   0x4ee4dc FireSpreadStep                 — fire propagation coroutine (5 states:
//                                             ignite SFX, nearest-neighbour pick,
//                                             ignite spread, douse, extinguish SFX).
//   0x4cbd04 EvaluateGroupCompositionState  — group-composition advisory message
//                                             (member roster walk, AiMethod eval,
//                                             per-member rumour-text concat).
//   0x4ca658 TavernSocializeState           — tavern "Stammtisch" socialise coroutine
//                                             (resolve building, join/leave group,
//                                             counter +192, timed re-arm).
//   0x4e7588 MasterExamPayStep              — master-exam fee-payment panel coroutine
//                                             (event-panel, form-event 1210/1155 fork,
//                                             affordability check, fee op).
//   0x4cc690 StartWanderSearchState         — journeyman-wander launch (handler count
//                                             gate >4, violation, RNG seeds at +196/
//                                             +200/+204/+208, paired vs solo event).
//   0x474070 EvaluateAssignProfession       — master AI: pick a profession/building to
//                                             assign an apprentice (category histogram,
//                                             RNG slot pick, target eval, cost check).
//   0x4ee288 GatherFollowersStep            — gather-followers (candidate roster, leader
//                                             pick, proximity recruit into +172.. slots).
//   0x4ccbb4 DetachFromGroupState           — detach two members from a group (delta
//                                             clear +0x5C, recruit-cost re-seed, re-arm).
//   0x4e6f4c DismissApprenticeStep          — dismiss apprentice (relation gate,
//                                             building ops, target carry, quickjump msg).
//
// Every +112/+120 transition, the GameTime stamping (+68/+82/+172/+176 14-byte
// images), the per-state Advance deltas, every RandomModulo / RandomFloatScaled draw
// (count + order), and the command emissions are 1:1 with the disassembly. All
// cross-cluster leaves (Person/Building/GameObject queries, the grid record arrays
// word_12CE910/dword_12CE914/dword_12CEA80, the command builders, the AiMethod /
// AiAction / BuildingType evaluators, the form-event globals dword_75BF04/0x75BF38,
// text/voice/history broadcast, and SelectBestRecursive-style sub-blocks) are routed
// through NpcAction10Hooks with inert defaults, mirroring the NpcAction3Hooks pattern
// so the machines run in isolation against a synthetic scene.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants (resolved byte-for-byte via get_bytes).
// ===========================================================================
extern const double kCreditChargeMul;    // dbl_61F720 = 0.01  (* +192 rate * +180)
extern const float  kKidnapWealthCap;    // flt_61FABC = 1604000.0 ransom wealth cap
extern const float  kKidnapRansomR1;     // rank-1 ransom factor 0.02
extern const float  kKidnapRansomR2;     // rank-2                0.04 (0.039999999)
extern const float  kKidnapRansomR3;     // rank-3                0.06 (0.059999999)
extern const float  kKidnapRansomR4;     // rank>=4               0.1
extern const float  kFireSpreadMaxDist;  // 1e8 initial nearest-distance sentinel
// Wander profession-seed table (dword_478410, 16 dwords) used by StartWanderSearch.
extern const i32    kWanderSeedTable[16];

// ===========================================================================
// Leaf hooks. nullptr installs an inert default (every effect a no-op, every query
// returns absent/zero). Tests install a recording/synthetic mock. A `void*` is the
// opaque Person/Building/GameObject/handler record handle the originals keep in a
// register; `objId(rec)` reads its +1 dword entity-id, `markerWord(rec)` its +0 word,
// `kind(rec)` its +2 byte (the 6/7 player/host gate), `recRank(rec)` its +433 byte.
// ===========================================================================
struct NpcAction10Hooks {
    // --- packet gating / handler lifecycle (shared) ---
    i32  (*packetStatus)(i32 handle);                 // GetPacketStatusById
    i32  (*queueEntity29)(int arg, HeRecord* h);      // QueueRequestEntity29 -> handle
    i32  (*freeHandlerEntry)(HeRecord* h);            // He_FreeHandlerEntry
    // VIBE_He_FindFirstHandlerByFilter(1,0,filter) then count via FindNext; returns
    // the number of OTHER handlers matching `filter` (StartWander gate uses >4), and
    // whether any matches the caller's entity (Fire/Detach/AssignProfession loops).
    int  (*countHandlers)(int filter);
    bool (*anyHandlerMatchesEntity)(int filter, i32 entityId);

    // --- entity record lookup / queries ---
    void* (*findPersonById)(i32 id);                  // Person_FindRecordById
    void* (*personQueryBegin)(i32 filterId);          // Person_QueryBegin(...,filterId)
    void* (*resolveEntity)(i32 id);                   // GameObject_ResolveEntityById
    void* (*gameObjectQueryFind)(void* base, int kind);// GameObject_QueryFind
    void* (*personFindActive)(void* rec);             // Person_FindActiveByEntity
    void* (*familyRecord)(void* rec);                 // Person_GetFamilyRecord
    // record field reads
    i32  (*objId)(void* rec);                         // *(rec+1)
    u16  (*markerWord)(void* rec);                    // *(rec+0)
    u8   (*kind)(void* rec);                          // *(rec+2)
    u8   (*recRank)(void* rec);                       // *(rec+433)
    bool (*hasCharacter)(void* rec);                  // *(rec+97)/+388 nonzero
    // grid record arrays (word_12CE910 person-by-cityIndex / dword_12CE914 city id /
    // dword_12CEA80 city-record): cityRecord(cityIndex) -> opaque, cityId(cityIndex).
    void* (*cityPersonRecord)(u16 cityIndex);         // &word_12CE910[268*idx]
    i32  (*cityId)(u16 cityIndex);                    // dword_12CE914[134*idx]
    void* (*cityAuxRecord)(u16 cityIndex);            // dword_12CEA80[134*idx]

    // --- money / wealth ---
    i32  (*sumCurrencyHeld)(void* rec);               // Person_SumCurrencyHeld
    i32  (*currencyAmount)(void* rec);                // Person_GetCurrencyAmount
    i32  (*computeTotalWealth)(void* rec);            // Person_ComputeTotalWealth
    void (*queueRequest16)(i32 fromId, i32 toId, i32 amt); // money transfer
    // VIBE_NpcAction_DistributeCreditToItems(payerId, payeeRec, msg, amount) -> taken.
    i32  (*distributeCredit)(i32 payerId, void* payeeRec, i32 amount);

    // --- command builders ---
    void (*requestCoord27)(i32 a, i32 b, int delta);
    void (*requestBuildOp91)(i32 id, int kind);
    void (*requestBuildOp71)(i32 id);
    void (*requestBuildOp77)(i32 id);
    void (*requestBuildOp90)(int a, i32 cityId);
    void (*requestNamedObject53)(i32 id, i32 objId, i32 target, int flag, const char* name);
    void (*queueSlotReset28)(int kind, i32 idA, i32 idB, i32 amount);
    void (*setEntityFieldM1)(void* rec, int offset);  // BeginDelta+Append(-1)+commit
    void (*enqueueBuildOp84)(const char* name, i32 idA, i32 idB);

    // --- text / voice / history / event-panel ---
    void (*sendMessage)(i32 targetId, int textId);    // Text_Render + He_SendEntityMessage
    void (*playSample)(int channel, const char* name);
    i32  (*eventPanelCreate)(HeRecord* h);            // EventPanel_CreateSlot -> ok
    i32  (*eventPanelDestroy)(HeRecord* h);           // EventPanel_DestroySlot
    void (*renderRichString)(int textId);             // Text_RenderRichString
    void* (*panelWindow)(HeRecord* h);                // *(rec+116) form window handle
    // form-event resolution: returns the pending form-event code for `window`
    //   (dword_75BF38: -1 none, 1210 = accept, 1155 = reject, other = dismiss);
    //   matchesWindow == (dword_75BF04 == window). The mock drives both.
    bool (*formEventMatches)(void* window);           // dword_75BF04 == window+8
    int  (*formEventCode)(void* window);              // dword_75BF38
    void (*historyWanderA)(void* cityRec);
    void (*historyWanderB)(void* rec);
    void (*historyWanderPair)(void* rec, void* cityRec);

    // --- AI / building-type evaluators (SelectBestRecursive-class leaves) ---
    // EvaluateGroupCompositionState: AiMethod_EvalGroupComposition(roster,1,n) -> ok.
    int  (*evalGroupComposition)(int memberCount);
    // EvaluateAssignProfession leaves:
    u8   (*mapActionToCategory)(int actionByte);
    int  (*computeVariantIndex)(int category);
    u8   (*mapToProfessionCode)(int variant);
    int  (*buildingSlotsWorth)(i32 buildingId);
    bool (*evalMeisterTarget)(int professionCode, void* personRec);
    int  (*buildingRating)(void* cityRec, int kind);  // Building_ComputeRatingCurveA /
                                                      // EvalProductionRating (float->int*100)
    // KidnapCarryStep ransom register + meister AP event.
    void (*registerApEvent)(u16 markerWord);
    // GatherFollowers: NpcAction_FindInteractionTarget-style + spatial proximity.
    bool (*withinTolerance)(void* recA, void* recB, float tol);
    // DismissApprentice: relation gate + carry target pick.
    int  (*relationEntry)(void* a, void* b);          // Relation_LookupMatrixEntry
    bool (*pickCarryTarget)(void* rec, i32* outObj, i32* outTarget);
    // StartWander: inventory slot active check; FireSpread: 348 carries.
    bool (*inventorySlotActive)(void* rec, int itemId);
    void (*evaluateViolation)(int crime, i32 victimId, i32 cityId, i32 perpId);
    // gameObject member roster walk for group composition (count + record by index).
    int  (*groupMemberCount)(void* group);
    void* (*groupMemberRecord)(void* group, int index);
};

void SetNpcAction10Hooks(const NpcAction10Hooks* hooks);
const NpcAction10Hooks& GetNpcAction10Hooks();

// ===========================================================================
// gilde.exe 0x4e5754 — VIBE_NpcAction_RunCreditStep(h@eax, begin@edi).
void NpcAction10_RunCreditStep(HeRecord* h);

// gilde.exe 0x4cb880 — VIBE_NpcAction_MasterExamState(h@eax). Returns the orig al.
i32  NpcAction10_MasterExamState(HeRecord* h);

// gilde.exe 0x4eab30 — VIBE_NpcAction_KidnapCarryStep(h@eax, esi). Returns orig eax.
i32  NpcAction10_KidnapCarryStep(HeRecord* h);

// gilde.exe 0x4ee4dc — VIBE_NpcAction_FireSpreadStep(h@eax, edi, esi).
void NpcAction10_FireSpreadStep(HeRecord* h);

// gilde.exe 0x4cbd04 — VIBE_NpcAction_EvaluateGroupCompositionState(h@eax). Orig eax.
i32  NpcAction10_EvaluateGroupCompositionState(HeRecord* h);

// gilde.exe 0x4ca658 — VIBE_NpcAction_TavernSocializeState(h@eax). Orig eax.
i32  NpcAction10_TavernSocializeState(HeRecord* h);

// gilde.exe 0x4e7588 — VIBE_NpcAction_MasterExamPayStep(h@eax, ebx). Orig eax.
i32  NpcAction10_MasterExamPayStep(HeRecord* h);

// gilde.exe 0x4cc690 — VIBE_NpcAction_StartWanderSearchState(h@eax). Orig eax.
i32  NpcAction10_StartWanderSearchState(HeRecord* h);

// gilde.exe 0x474070 — VIBE_NpcAction_EvaluateAssignProfession.
//   prevResult@al, person@edx, outAction@ecx, relFlag@bl, outExtra@esp.
//   Returns 52 (assign) or 0 (none). On 52 it writes outAction[0..5] + outExtra.
u8   NpcAction10_EvaluateAssignProfession(u8 prevResult, void* person,
                                          i32* outAction, u8 relFlag, i32* outExtra);

// gilde.exe 0x4ee288 — VIBE_NpcAction_GatherFollowersStep(h@eax, esi). Orig eax.
i32  NpcAction10_GatherFollowersStep(HeRecord* h);

// gilde.exe 0x4ccbb4 — VIBE_NpcAction_DetachFromGroupState(h@eax). Orig eax.
i32  NpcAction10_DetachFromGroupState(HeRecord* h);

// gilde.exe 0x4e6f4c — VIBE_NpcAction_DismissApprenticeStep(h@eax, esi). Orig eax.
i32  NpcAction10_DismissApprenticeStep(HeRecord* h);

} // namespace guild::sim
