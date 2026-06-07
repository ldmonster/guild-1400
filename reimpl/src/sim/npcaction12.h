#pragma once
// NpcAction12 — the final batch of UNTRANSLATED deterministic VIBE_NpcAction_*
// behaviours (the remaining slice after npcaction.cpp..npcaction11.cpp).
//
// This batch finishes the leftover step/launcher coroutines flagged by the wave:
// the alliance/group-forming social broadcasts (FormAllianceGroup), the work-place
// assignment and demolition/master-exam/training step machines, the ranged-attack
// front-evaluator twin (EvaluateUseFront, the mirror of npcaction11's
// EvaluateUseBack), the hair-gesture wander/sound behaviour, the relation-follow
// launcher (BeginFollowTarget), the tavern stammtisch join/leave command emitter,
// the conflict-scan launchers (BeginScanType50/63), the appointment-arming
// launchers (InitWalkState / BeginGotoHomeStep / InitDualCoordWalk), the
// wander-path coordinate planner (ComputeWanderPathCoords), the worker-quarters
// office build/upgrade (BuildWorkerQuarters), and the LCG-driven random-action
// queuer (QueueRandomActions).
//
// Translated 1:1 against the Hex-Rays pseudocode (disasm-resolved where the
// decompiler emitted uninitialised-local artifacts for the implicit `this`
// register):
//
//   0x568fac FormAllianceGroup        — pick 3 distinct eligible non-self peers
//                                        (RandomModulo(0x300), strides 1/7/13,
//                                        768-slot caps), coord27(+20) each, broadcast
//                                        text 3252 to host-kind picks, panel 3245.
//   0x4e7184 AssignWorkPlaceStep      — state 0: resolve person, relation gate
//                                        (>-26 abort), find a work object (kind
//                                        42/278), pick a random product slot, sell
//                                        it (request17) + quickjump 6086/1425.
//   0x4718d4 EvaluateUseFront         — front-attack AI: gun-cooldown gate (+485&1),
//                                        SelectBestRecursive(40)/TrySingleAttack/
//                                        SelectBestRecursive(41). Copies 24-byte outs.
//   0x4c94b4 HairGestureBehavior      — global gate dword_649D60; per member (+140),
//                                        40% sound-gesture else build a wander path
//                                        and tag CharAction nodes "HeCharacterBeh".
//   0x4e4cf0 DemolishBuildingStep     — state+2 switch: 2 = recall workers + stamp
//                                        +82 (+5min) advance; 3 = host-peer notices
//                                        (6239/1419) + single59; else free.
//   0x4e5c24 MasterExamStep           — appointment-gated (Compare +82): state 0 =
//                                        open event slot + voice/text exam result;
//                                        state 1 = panel-result dispatch; else free.
//   0x4e63dc NotifyTrainingStep       — state 0: resolve person, render training
//                                        message (table byte_13CD6A0/dword_13CD6F2),
//                                        sendEntity 1418, request17 sell, free.
//   0x4e64f8 BeginFollowTarget        — stamp +82 (+1s); resolve leader, amt record,
//                                        target; change player action; mixed44 cmd.
//   0x4746f8 TavernJoinLeave          — switch on the +8 magic tag (JOIN/LEAV/'0pra')
//                                        -> stammtisch build-op + args25(0x2000000).
//   0x4eb490 BeginScanType63          — scan filter-63 handlers for a +172 conflict;
//                                        abort (+112=-1) if found; stamp +82 (+1s).
//   0x4e6ea8 BeginScanType50          — scan filter-50 handlers; if the self handler
//                                        is the only match, stamp +82 (+1s); else free.
//   0x4e7810 InitWalkState            — +176=3, +172=0, season probe, stamp +82/+68,
//                                        GameTime_Set(+82, 6h,0,15m).
//   0x4e8b88 BeginGotoHomeStep        — stamp +82, Set(+82, 5h,0,0), copy to +96,
//                                        advance +1 day.
//   0x4ecfb0 InitDualCoordWalk        — stamp +82 and +204; advance +204 +48 days,
//                                        +82 +1 second.
//   0x4ccad4 ComputeWanderPathCoords  — office-rank base + wealth-clamped budget,
//                                        shuffle 17 dwords, write 3 member ranks
//                                        (+188) and scaled coords (+ flt_61E99C).
//   0x4725c0 BuildWorkerQuarters      — office-storage gate (+358==15); upgrade(4):
//                                        slotWorth*flt_61A594 cmd15; else build via
//                                        AiAction_LoadBuildingGraphic. Returns 46/0.
//   0x5766d4 QueueRandomActions       — LCG (dword_12335D0 = 1103515245*x+12345):
//                                        draw count in [0,a1], re-seed timeGetTime,
//                                        emit that many slot-reset-28(69). Ret count.
//
// Every +112/+120 transition, the GameTime stamping into +82/+68/+96/+204, each
// Advance/Set delta, each RandomModulo / LCG draw, and every command emission is
// 1:1 with the disassembly. Cross-cluster leaves are routed through
// NpcAction12Hooks with inert defaults defined in this library .cpp (the
// NpcAction10/11Hooks pattern), so the machines run in isolation against a
// synthetic scene. The shared global game clock is the existing NpcClock()
// (npcaction.cpp); GameTime_Advance/Compare are reused via extern.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants (resolved byte-for-byte via get_bytes).
// ===========================================================================
extern const float  kWorkerWorthMul;   // flt_61A594 = worker-quarters upgrade cost factor
extern const float  kWanderCoordScale; // flt_61E99C = wander-path coord radius scale

// LCG used by QueueRandomActions (dword_12335D0). Exposed so the itest can seed it
// and reproduce the draw sequence; it is the classic glibc constants.
extern u32 g_lcgState;                  // dword_12335D0
constexpr u32 kLcgMul = 1103515245u;
constexpr u32 kLcgAdd = 12345u;

// ===========================================================================
// Leaf hooks. nullptr installs an inert default (every effect a no-op, every query
// returns absent/zero). `void*` is the opaque Person/Building/GameObject record the
// originals keep in a register; the field accessors below read it by byte offset.
// ===========================================================================
struct NpcAction12Hooks {
    // --- handler lifecycle / scanning ---
    i32  (*freeHandlerEntry)(HeRecord* h);              // He_FreeHandlerEntry
    // He_FindFirstHandlerByFilter(1,0,filter) + FindNext loop. Returns the first
    // OTHER handler whose +172 (43*4) matches `target172`, else nullptr.
    void* (*findConflictingHandler)(HeRecord* self, int filter, i32 target172);
    // Like above but reports whether ANY filter-50 match other than self exists.
    bool (*scanFilterHasForeignMatch)(HeRecord* self, int filter);

    // --- entity record lookup / queries ---
    void* (*findPersonById)(i32 id);                    // Person_FindRecordById
    void* (*personQueryBegin)(i32 filterId);            // Person_QueryBegin(...,filterId)
    void* (*buildingFindOfficeStorage)(int kind, HeRecord* h); // Building_FindOfficeStorage
    void* (*buildingFindWorkProduct)(void* building);   // Building_FindWorkProductObject
    void  (*resolveEntity)(i32 id, void** out);         // GameObject_ResolveEntityById
    void* (*gameObjectQueryFind)(i32 base, int a, int b, int c, int d); // QueryFind
    void* (*gameObjectIterFirst)(i32 base, int a, int b); // QueryFind(base,1,5)
    void* (*gameObjectIterNext)();                      // GameObject_IterNext
    void* (*amtFindRecordByKey)(void* key, i32 seq);    // Amt_FindRecordByKey

    // --- record field reads (byte offsets off the opaque record) ---
    i32  (*objId)(void* rec);                           // *(rec+4)  entity id
    u16  (*markerWord)(void* rec);                      // *(rec+0)  city/person word
    u8   (*kind)(void* rec);                            // *(rec+2)  the 3/4/5/6/7 gate byte
    u8   (*equipState)(void* rec, int off);             // *(rec+off) general byte read
    i32  (*field)(void* rec, int off);                  // *(int*)(rec+off) general dword read

    // --- person stats / relation / pricing ---
    i32  (*relationLookup)(u16 a, u16 b);               // Relation_LookupMatrixEntry
    i32  (*computeOfficeRank)(u16 person, int mode);    // Person_ComputeOfficeRank
    i32  (*computeTotalWealth)(u16 person, void* ctx);  // Person_ComputeTotalWealth
    i32  (*computeRankWithinGroup)(u8 code);            // BuildingType_ComputeRankWithinGroup
    u8   (*groupFromCode)(u8 code);                     // BuildingType_GroupFromCode
    double (*lookupMarketPrice)(i16 good, u8 currency); // Building_LookupCachedMarketPrice
    i32  (*sumFlaggedSlotsWorth)(int group);            // Building_SumFlaggedSlotsWorth
    void (*shuffleDwords)(int n, i32* dst);             // Util_InitAndShuffleDwordArray

    // --- command builders (emit-only) ---
    void (*requestCoord27)(i32 a, i32 b, int delta);    // QueueRequestCoord27
    void (*request17)(i32 idA, i32 idB, int count, int kind, u8 cur, int f); // QueueRequest17
    void (*requestSingle59)(i32 id);                    // QueueRequestSingle59
    void (*requestArgs25)(i32 id, int a, int b, int c, int d); // QueueRequestArgs25
    void (*requestNamedObject53)(i32 id, i32 objId, int a, i32 b, int c, const char* name);
    void (*requestBuildOp77)(i32 id);                   // RequestBuildOp77
    void (*requestBuildOp84)(const void* key);          // RequestBuildOp84
    void (*requestSlotReset28)(void* slotImage, int n); // QueueRequestSlotReset28
    void (*buildingActionStart)(const char* name);      // EnqueueBuildingActionStart
    void (*buildingActionEnd)();                        // EnqueueBuildingActionEnd
    void (*enqueueCmd15)(i32 idA, i32 idB, long long amount, u8 cur); // EnqueueCmd15
    i32  (*queueRequestMixed44)(i32 a, u8 b, u16 c, u8 d, int e, i32 f); // QueueRequestMixed44
    bool (*combatPickActiveTarget)(void* peer, i32* outA, i32* outB);   // Combat_PickActiveTargetEntry
    bool (*aiLoadBuildingGraphic)(void* objA, void* objB);             // AiAction_LoadBuildingGraphic

    // --- text / voice / panel / dialog ---
    void (*sendEntity)(i32 targetId, int textId);       // Text_Render + He_SendEntityMessage
    void (*sendQuickjump)(i32 cityId, int textId, i32 a, i32 b, const char* tag); // SendQuickjump
    void (*panelShowAlliance)(int textId, u16 self, u16 a, u16 b, u16 c);          // Panel_ShowUseObject
    void (*eventPanelCreate)(HeRecord* h, int a, int textId);          // EventPanel_CreateSlot
    void (*eventPanelDestroy)(HeRecord* h);             // EventPanel_DestroySlot
    void (*playExamVoice)(bool passed, int rank);       // Sound_FindBankByName + Voice
    void (*renderExamResult)(int kind, u16 self, int a, int b, int c); // Text_RenderRichString
    void (*dialogOpenBuilding)(i32 a, i32 b);           // Dialog_OpenBuildingForActiveChar
    void (*changePlayerAction)(void* self, void* obj, HeRecord* h, u16 marker);    // Character_ChangePlayerAction
    void (*createSoundAction)(i32 entity, int variant); // Character_CreateSoundAction
    // Hair-gesture wander build: returns count, fills coords (float[4]*count).
    int  (*animalBuildWanderPath)(float* dst, int len, float* coordsOut);
    bool (*heightmapWorldToTile)(i32 ctx, float* coord, i32* outXY, float* outZ);  // Heightmap_WorldToTileWithHeight
    void* (*charActionInsert)(i32 entity, i32 x, i32 y);// CharAction_InsertActionVararg (returns node)

    // --- gates / RNG ---
    u16  (*randomModulo)(u16 n);                        // Math_RandomModulo
    bool (*gateHairGesture)();                          // dword_649D60 != 0 (suppresses gesture)
    bool (*tryRangedAttack)(void* self, void* out);     // AiPlayer_TrySingleAttack
    u8   (*selectBestRecursive)(int method, u16 self, void* a, void* b);           // AiMethod_SelectBestRecursive
    // city-grid arrays (per-city). word_12CE910 / byte_12CE912 / dword_12CE914 /
    // byte_12CEA76 (alliance-eligible flag). Index 0..767.
    u16  (*cityMarker)(u16 idx);                        // word_12CE910[268*idx]
    u8   (*cityKind)(u16 idx);                          // byte_12CE912[536*idx]
    i32  (*cityId)(u16 idx);                            // dword_12CE914[134*idx]
    i32  (*cityIdShifted)(u16 idx);                     // (dword_12CE914[134*idx]+2)>>24 (alliance group word)
    u8   (*cityAllianceEligible)(u16 idx);              // byte_12CEA76[536*idx]
    void* (*cityRecord)(u16 idx);                       // &word_12CE910[268*idx]
};

void SetNpcAction12Hooks(const NpcAction12Hooks* hooks);
const NpcAction12Hooks& GetNpcAction12Hooks();

// ===========================================================================
// Translated functions. Each returns the original al/eax where the caller uses it.
// ===========================================================================

// gilde.exe 0x568fac — VIBE_NpcAction_FormAllianceGroup(self@eax, ctx@edx). Ret 1.
i32 NpcAction12_FormAllianceGroup(void* self, i32* ctx);

// gilde.exe 0x4e7184 — VIBE_NpcAction_AssignWorkPlaceStep(h@eax). Orig eax.
i32 NpcAction12_AssignWorkPlaceStep(HeRecord* h);

// gilde.exe 0x4718d4 — VIBE_NpcAction_EvaluateUseFront(person@edx, outA@ecx,
//   relFlag@bl, outB@esp). Returns 0 / selected-method / 41. Writes 24-byte outs.
u8 NpcAction12_EvaluateUseFront(void* person, void* outA, u8 relFlag, void* outB);

// gilde.exe 0x4c94b4 — VIBE_NpcAction_HairGestureBehavior(members@eax, coordCtx@edx).
//   `members` is the base of the 8-slot +140 member-id array (8 dwords). Ret 0.
i32 NpcAction12_HairGestureBehavior(HeRecord* h, float* coordCtx);

// gilde.exe 0x4e4cf0 — VIBE_NpcAction_DemolishBuildingStep(h@eax). Orig eax.
i32 NpcAction12_DemolishBuildingStep(HeRecord* h);

// gilde.exe 0x4e5c24 — VIBE_NpcAction_MasterExamStep(h@eax). Orig eax.
i32 NpcAction12_MasterExamStep(HeRecord* h);

// gilde.exe 0x4e63dc — VIBE_NpcAction_NotifyTrainingStep(h@eax). Orig eax.
i32 NpcAction12_NotifyTrainingStep(HeRecord* h);

// gilde.exe 0x4e64f8 — VIBE_NpcAction_BeginFollowTarget(h@eax). Orig ax.
i32 NpcAction12_BeginFollowTarget(HeRecord* h);

// gilde.exe 0x4746f8 — VIBE_NpcAction_TavernJoinLeave(self@eax, action@edx).
//   Returns 53 on a recognised tag, else 0.
u8 NpcAction12_TavernJoinLeave(void* self, void* action);

// gilde.exe 0x4eb490 — VIBE_NpcAction_BeginScanType63(self).
i32 NpcAction12_BeginScanType63(HeRecord* h);

// gilde.exe 0x4e6ea8 — VIBE_NpcAction_BeginScanType50(self).
i32 NpcAction12_BeginScanType50(HeRecord* h);

// gilde.exe 0x4e7810 — VIBE_NpcAction_InitWalkState(h@eax).
i32 NpcAction12_InitWalkState(HeRecord* h);

// gilde.exe 0x4e8b88 — VIBE_NpcAction_BeginGotoHomeStep(h@eax).
i32 NpcAction12_BeginGotoHomeStep(HeRecord* h);

// gilde.exe 0x4ecfb0 — VIBE_NpcAction_InitDualCoordWalk(h@eax).
i32 NpcAction12_InitDualCoordWalk(HeRecord* h);

// gilde.exe 0x4ccad4 — VIBE_NpcAction_ComputeWanderPathCoords(members@eax,
//   selfPerson@edx, peerPerson@ebx). `members` is the +0 base of the 3-pair
//   member word/coord block. Returns the last scaled coord.
i32 NpcAction12_ComputeWanderPathCoords(i16* members, u16 selfPerson, u16 peerPerson);

// gilde.exe 0x4725c0 — VIBE_NpcAction_BuildWorkerQuarters(h@eax, action@ebx,
//   building@edx). Returns 0 (gate/fail) or 46 (queued).
u8 NpcAction12_BuildWorkerQuarters(HeRecord* h, u8* action, void* building);

// gilde.exe 0x5766d4 — VIBE_NpcAction_QueueRandomActions(maxCount@eax, arg@edx).
//   Returns the drawn count (0 if maxCount == -1).
i32 NpcAction12_QueueRandomActions(i32 maxCount, i32 arg);

} // namespace guild::sim
