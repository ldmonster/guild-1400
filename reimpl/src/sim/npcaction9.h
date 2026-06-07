#pragma once
// NpcAction9 — the AI "candidate-scoring / action-evaluation" leaf family of the
// NpcAction cluster (gilde.exe), batch 9. This continues the npcaction8.cpp family
// of per-tick NPC action *evaluators*: given the actor's Person/object record and a
// per-action relation flag (the `bl` byte), each evaluator gates on world state,
// optionally probes the world for a target, fills one or two 24-byte (0x18) action
// "request" blocks, and returns the action code the dispatcher uses to pick a winner
// (0 == "this action is not applicable this tick").
//
// Unlike the npcaction8 score-pair evaluators (which write two out floats), this
// batch is the *combat / economy / law / recruitment* slice. They share a single
// recurring shape:
//
//   * a2@<edx> : the actor Person record (raw bytes; the originals read it as
//                *(_TYPE*)(record + off)).
//   * a3 / a2  : the primary 24-byte request block to fill (the action target).
//   * a4@<bl>  : the relation flag (most evaluators bail when it is non-zero; a few
//                compare it against a specific code, e.g. EvaluateThrow wants 2).
//   * a5       : a secondary 24-byte request block (filled by some evaluators) or a
//                small in/out byte (EvaluateUseItemOnTarget / EvaluateThrowAtRival
//                read *a5 as an action sub-code 15/18).
//   * a1@<al>  : a "previous evaluator result" byte threaded through the per-tick
//                table; a non-zero value usually short-circuits to 0, and several
//                evaluators additionally branch on the actor's guild-rank/profession.
//
// Every cross-module callee — the AI candidate selector (AiMethod_SelectBestRecursive
// @0x..), the object/person world queries, the market-price / wealth / currency
// leaves, the RNG (Math_RandomModulo / Math_RandomFloatScaled), the command emitters,
// etc. — is routed through NpcAction9Hooks with inert defaults defined in
// npcaction9.cpp. Tests install a recording mock; nullptr restores the inert
// defaults. The pure deterministic cores (the float gates and integer formulas) are
// exposed for golden tests.
//
// `Coord_ConvertX` in the originals is the x87 FPU control-word fixup around a
// `(int)double` store: it truncates toward zero. We model it with TruncToInt.
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x470d38 EvaluateShoot            0x470f24 EvaluateThrow
//   0x4710c4 EvaluateThrowAtRival     0x471380 EvaluateUseItemOnTarget
//   0x471650 EvaluateEnterBuilding    0x4730cc EvaluateBribeJailed
//   0x47285c EvaluateRecruitWorker    0x472c8c EvaluateRecruitFromBuilding
//   0x472720 EvaluateHirePersonnel    0x474c4c EvaluateArrest
//   0x473700 EvaluateShopInteract     0x474340 EvaluateSocializeGroup
#include "guild/common/types.h"

namespace guild::sim {

// The action request block every evaluator marshals and qmemcpy's out (0x18 bytes).
// We model it as opaque 24-byte storage; the originals write a byte at [0] and dwords
// at [4],[8],[16],[20] (the v[0]/v[1]/v[2]/v[4]/v[5] dword-array slots).
constexpr int kNpc9ReqBlockSize = 24;   // 0x18

// ---------------------------------------------------------------------------
// dword_478450 — the 16-entry coprime *stride* table SocializeGroup probes the
// 256-slot person grid with (so the random start walks the whole grid). Exposed
// for the golden test.
// ---------------------------------------------------------------------------
extern const u32 kSocializeStrideTable[16];

// ===========================================================================
// Recovered float constants (imagebase 0x400000). Exposed for golden tests.
// ===========================================================================
constexpr double kShootCloudBias    = 10.0;     // dbl_61A4F0
constexpr float  kShootWealthMul    = 0.005f;   // flt_61A4F8
constexpr float  kShootCurrencyMul  = 0.22f;    // flt_61A4FC
constexpr float  kRivalWealthMul    = 0.005f;   // flt_61A50C
constexpr float  kRivalFavBase      = 200.0f;   // flt_61A510
constexpr float  kRivalFavMul       = 0.01f;    // flt_61A514
constexpr double kRivalScale        = 0.01;     // dbl_61A518
constexpr double kRivalLoClamp      = 3200.0;   // dbl_61A520
constexpr double kRivalHiClamp      = 320000.0; // dbl_61A528
constexpr float  kRivalAffordMul    = 0.44f;    // flt_61A530
constexpr float  kRecruitHiThresh   = 5000.0f;  // flt_61A598 / flt_61A5B8
constexpr float  kRecruitLoThresh   = 1250.0f;  // flt_61A59C / flt_61A5BC
constexpr float  kRecruitWorthMul   = 0.4f;     // flt_61A5A0 / flt_61A5C0
constexpr float  kSocializeNoneProb = 0.33f;    // flt_61A62C
constexpr float  kSocializeLeaveProb= 0.75f;    // flt_61A630
constexpr float  kSocializeFavGate  = 0.666f;   // flt_61A634
constexpr float  kSocializeFavBias  = 0.166667f;// flt_61A638
constexpr float  kArrestWealthMul   = 0.01f;    // flt_61A674
constexpr float  kArrestWealthFloor = 160.0f;   // flt_61A678
constexpr float  kArrestCurrencyMul = 0.22f;    // flt_61A67C

// ===========================================================================
// Cross-module leaf hooks for the NpcAction9 family. Records are raw byte
// buffers, matching the originals' *(_TYPE*)(base + off). nullptr / 0 models an
// empty world (every evaluator then takes its earliest "not applicable" exit).
// ===========================================================================
struct NpcAction9Hooks {
    // --- world / object queries ---
    // VIBE_GameObject_QueryFind(slot, a, b, [id], [tag]) (0x5857fc). The 4-arg and
    //   5-arg call sites both reach the same leaf; we expose the widest form (id and
    //   tag default 0 when a caller passes fewer). Returns a record pointer or null;
    //   the first word is the resolved object id.
    const u16* (*gameObjectQueryFind)(int slot, int a, int b, int id, int tag);
    // VIBE_GameObject_ResolveEntityById(out, a, entityId, c) (0x..) — writes the
    //   resolved record pointer into *out (0 if none).
    void (*resolveEntityById)(void** out, int a, int entityId, int c);
    // VIBE_GameObject_SumValuesAtLocation(loc) (0x..). Inert => 0.
    int (*sumValuesAtLocation)(int loc);

    // --- candidate selection (the heart of these evaluators) ---
    // VIBE_AiMethod_SelectBestRecursive(method, actorId, reqA[24], flag, reqB[24])
    //   (0x..). Returns the action code (non-zero on success). The inert default
    //   returns 0 (no candidate chosen).
    int (*selectBestRecursive)(int method, u16 actorId, void* reqA, int flag, void* reqB);
    // VIBE_AiAction_EvalMeisterTarget(reqA[24], actorRecord, reqB[24], flag) (0x..).
    //   Returns non-zero on success. Inert => 0.
    int (*evalMeisterTarget)(void* reqA, const void* actorRecord, void* reqB, int flag);
    // VIBE_AiPlayer_TryGroupAttack(actorRecord, reqA[24], scratch, reqB[24]) (0x..).
    int (*tryGroupAttack)(const void* actorRecord, void* reqA, int scratch, void* reqB);
    // VIBE_AiPlayer_FindRivalToConfront(actorRecord, outA[24], outB[24]) (0x47d970).
    int (*findRivalToConfront)(const void* actorRecord, void* outA, void* outB);
    // VIBE_AiPlayer_FindOpponentBuilding(actorRecord, outA[24], outB[24]) (0x..).
    int (*findOpponentBuilding)(const void* actorRecord, void* outA, void* outB);
    // VIBE_AiObject_CountInventoryMatch(outDesc, outId, record) (0x..). Returns the
    //   match count (non-zero on hit) and writes a descriptor and an id (-1 == none).
    int (*countInventoryMatch)(void* outDesc, int* outId, const void* record);

    // --- nearest-entity probes ---
    // VIBE_ObjectSearch_FindNearestEntity(record, kind, reqBlock, minR, maxR, outId)
    //   (0x..). Returns non-zero on hit and writes the entity id into *outId.
    int (*findNearestEntity)(const void* record, int kind, void* reqBlock,
                             float minR, float maxR, int* outId);
    // VIBE_ObjectSearch_FindMatchingColors(record, mode, scratch, params, minR, maxR,
    //   outId) (0x..). Returns non-zero on hit and writes the slot index into *outId.
    int (*findMatchingColors)(const u16* record, unsigned mode, int scratch,
                              const void* params, float minR, float maxR, int* outId);

    // --- person / family queries ---
    // VIBE_Person_FindRecordById(id) (0x58bc6c). Inert => null.
    const u16* (*personFindRecordById)(int id);
    // VIBE_Person_GetFamilyRecord(record) (0x..). Inert => null.
    const u16* (*personGetFamilyRecord)(const void* record);
    // VIBE_Person_QueryBegin(a, b, c, d) (0x..) / VIBE_Person_IterNext (0x..) —
    //   a person iteration cursor. begin returns the first record (or null); next
    //   advances. VIBE_Person_QueryByGoodType(kind, predicate) (0x..) — a filtered
    //   single-shot query returning one record or null.
    const u8* (*personQueryBegin)(int a, int b, int c, int d);
    const u8* (*personIterNext)();
    const u8* (*personQueryByGoodType)(int kind, const void* predicate);

    // --- wealth / currency / economy ---
    // VIBE_Person_GetCurrencyAmount(record, currency) (0x..). Inert => 0.
    int (*personCurrencyAmount)(int record, u8 currency);
    // VIBE_Person_ComputeTotalWealth(id, record) (0x..). Inert => 0.
    int (*personTotalWealth)(int id, const void* record);
    // VIBE_Ai_ComputePersonFavorability(idA, idB, mode) (0x594330). Inert => 0.
    float (*computePersonFavorability)(u16 idA, u16 idB, int mode);
    // VIBE_Economy_LoadDemandSnapshot(out[>=20 floats]) — RecruitWorker reads out[6]
    //   (the v18 = out at +0x18). Inert zero-fills.
    void (*loadDemandSnapshot)(float* out);
    // VIBE_Building_LookupCachedMarketPrice(itemHiword, currency) (0x58f6b8).
    double (*lookupCachedMarketPrice)(int itemHiword, u8 currency);
    // VIBE_Building_SumFlaggedSlotsWorth(typeCode) (0x..). Inert => 0.
    int (*buildingSumFlaggedSlotsWorth)(int typeCode);
    // VIBE_Building_GetCategoryForObject(objId) (0x..). Inert => 0.
    int (*buildingCategoryForObject)(int objId);
    // VIBE_Building_FindActiveWorkSlot(buildingId) (0x..). Inert => null.
    const u16* (*buildingFindActiveWorkSlot)(int buildingId);
    // VIBE_Building_FindOfficeStorage(kind, record) (0x..). Inert => null.
    const u16* (*buildingFindOfficeStorage)(int kind, const void* record);
    // VIBE_Building_ComputeRatingCurveA(kind) (0x..). Inert => 0.
    float (*buildingRatingCurveA)(int kind);
    // VIBE_BuildingType_ComputeRankWithinGroup(typeCode) (0x58a560). Inert => 0.
    int (*buildingRankWithinGroup)(int typeCode);
    // VIBE_Inventory_GetEffectiveStock(slot, objRecord) (0x..). Inert => 0.
    int (*inventoryEffectiveStock)(const u16* slot, int objRecord);

    // --- law / guild ---
    // VIBE_Straftat_CountActiveByTarget(id) (0x..). Inert => 0.
    int (*straftatCountActiveByTarget)(int id);
    // VIBE_He_SumPlayerHandlerValues(id) (0x..). Inert => 0.
    int (*heSumPlayerHandlerValues)(int id);
    // VIBE_He_FindFirstHandlerByFilter(a, b, c, [d], [e]) (0x..). Inert => null.
    const u8* (*heFindFirstHandlerByFilter)(int a, int b, int c, int d, int e);
    // VIBE_He_FindNextMatchingHandler() (0x..). Inert => null.
    const u8* (*heFindNextMatchingHandler)();
    // VIBE_Amt_CheckGuildRankLevel2(record) (0x..). Inert => 0.
    int (*amtCheckGuildRankLevel2)(const void* record);

    // --- RNG ---
    // VIBE_Math_RandomModulo(n) (0x58b89c). Inert => 0.
    u16 (*randomModulo)(u16 n);
    // VIBE_Math_RandomFloatScaled() (0x58b910), in [0,1). Inert => 0.0f.
    float (*randomFloatScaled)();

    // byte_6477A1 — the active player's currency byte.
    u8 currencyByte;

    // qword_13CE852 — low dword and WORD2 of the 14-byte game clock image
    //   (ThrowAtRival / BribeJailed gate on these). Inert defaults 0.
    int clockLow;
    int clockWord2;
};

void SetNpcAction9Hooks(const NpcAction9Hooks* hooks);
const NpcAction9Hooks& GetNpcAction9Hooks();

// ===========================================================================
// Evaluators. `reqA`/`reqB` point at caller-owned 24-byte blocks. Each returns the
// action code, or 0 ("not applicable this tick").
// ===========================================================================

// gilde.exe 0x470d38 — EvaluateShoot. record@edx, relFlag@bl, reqA. If relFlag set
//   -> 0. Find a same-color match within range; the affordability gate compares the
//   combined wealth estimate against currency*0.22; on pass write reqA (code 7) and
//   zero reqB, return 37; else 0.
int NpcAction9_EvaluateShoot(const u16* record, u8 relFlag, void* reqA, void* reqB);

// gilde.exe 0x470f24 — EvaluateThrow. record@ecx (the descriptor), relFlag@bl (must
//   be 2), actorId@edx, reqA. Gate on descriptor[0]==2, resolve a work slot + object,
//   require stock>=1, and check currency >= cached market price (with an optional
//   per-unit scale). On pass write reqA (code 1) and return 12; else 0.
int NpcAction9_EvaluateThrow(const u8* descriptor, u8 relFlag, int actorId, void* reqA);

// gilde.exe 0x4710c4 — EvaluateThrowAtRival. record@edx, reqA@ecx, relFlag@bl,
//   scratch@esi, reqB(byte). Bails when relFlag set or the per-NPC clock-phase guard
//   fails. Collects up to N rival persons, picks one, and for guild-rank<4 computes a
//   clamped "bounty" and writes a code-4 reqA + sub-code (18) into *reqB, returning
//   38; the rank>=4 branch writes a different count and sub-code 15.
int NpcAction9_EvaluateThrowAtRival(const u16* record, void* reqA, u8 relFlag,
                                    int scratch, u8* reqB);

// gilde.exe 0x471380 — EvaluateUseItemOnTarget. actorId@eax, reqIn@edx (the action
//   the previous step staged), subCode@ebx (*a3, 15 or 18). Resolves the target
//   person, then for sub-code 18 emits the arrest/seize command chain and for 15 the
//   use-item command chain. Returns 38 on either path, else 0.
int NpcAction9_EvaluateUseItemOnTarget(int actorId, const u8* reqIn, const u8* subCode);

// gilde.exe 0x471650 — EvaluateEnterBuilding. record@edx, reqA@ecx, relFlag@bl, reqB.
//   If relFlag set -> 0. Three branches (no active building / type==3 / has building),
//   each builds a SelectBestRecursive(method 40) request or a group-attack request;
//   on success copies the two blocks and returns the action code (40) / SelectBest's
//   result, else 0.
int NpcAction9_EvaluateEnterBuilding(const u16* record, void* reqA, u8 relFlag, void* reqB);

// gilde.exe 0x4730cc — EvaluateBribeJailed. prevResult@al, record@edx, reqA@ecx,
//   relFlag@bl, reqB. Gated on the clock, relFlag, record[+358]==10, prevResult==0,
//   a "jailer" person, and a vault-value roll. Scans the 256 jailed persons; the
//   first eligible one yields a SelectBestRecursive(method 15) candidate (code via
//   that call); otherwise falls back to one of three direct code-47 requests or a
//   meister-target request. Returns the action code (47 / SelectBest result) or 0.
int NpcAction9_EvaluateBribeJailed(u8 prevResult, const u16* record, void* reqA,
                                   u8 relFlag, void* reqB);

// gilde.exe 0x47285c — EvaluateRecruitWorker. prevResult@al, record@edx, reqA@ecx,
//   relFlag@bl, reqB. Gated on relFlag, record[+358]==15, prevResult==0, and (if
//   prevResult) profession 6/7. Finds the nearest entity, then tries a recruit
//   candidate (method 15), an inventory-match candidate (method 21), and finally a
//   demand-gated economic build (code 49). Returns the action code or 0.
int NpcAction9_EvaluateRecruitWorker(u8 prevResult, const u16* record, void* reqA,
                                     u8 relFlag, void* reqB);

// gilde.exe 0x472c8c — EvaluateRecruitFromBuilding. Byte-for-byte the RecruitWorker
//   shape but keyed on an office-storage building (FindOfficeStorage), item types
//   17/18, and returns code 60.
int NpcAction9_EvaluateRecruitFromBuilding(u8 prevResult, const u16* record, void* reqA,
                                           u8 relFlag, void* reqB);

// gilde.exe 0x472720 — EvaluateHirePersonnel. prevResult@al, record@edx, reqA@ecx,
//   relFlag@bl, reqB. Gated on relFlag, record[+358]==15, prevResult==0, and (if
//   prevResult) profession 6/9. Tries up to 4 nearest entities for an inventory-match
//   candidate (method 21); on success copies blocks and returns that code, else 0.
int NpcAction9_EvaluateHirePersonnel(u8 prevResult, const u16* record, void* reqA,
                                     u8 relFlag, void* reqB);

// gilde.exe 0x474c4c — EvaluateArrest. prevResult@al, reqA@ecx, relFlag@bl,
//   record@edx. Bails if relFlag/prevResult set or the guild-rank check != 1. Computes
//   a clamped bounty, checks affordability, finds the nearest target within a random
//   radius, requires it to be referenced in the city table, then writes a code-4 reqA
//   and returns 55, else 0.
int NpcAction9_EvaluateArrest(u8 prevResult, void* reqA, u8 relFlag, const i16* record);

// gilde.exe 0x473700 — EvaluateShopInteract. prevResult@al, reqA@ecx, relFlag@bl,
//   record@edx, reqB. Gated on prevResult in {0,37,50} and relFlag clear, plus a
//   building-category rank>2. Tries FindRivalToConfront then FindOpponentBuilding;
//   on hit copies the two blocks and returns 50, else 0.
int NpcAction9_EvaluateShopInteract(u8 prevResult, void* reqA, u8 relFlag,
                                    const u16* record, u8* reqB);

// gilde.exe 0x474340 — EvaluateSocializeGroup. prevResult@al, record@edx, reqA@ecx,
//   relFlag@bl, reqB. Bails if record[+459]&2, prevResult, or relFlag set. Walks the
//   256-slot person grid with a random coprime stride collecting up to 4 nearby
//   groups, evaluates average favorability, and emits one of several code-53 requests
//   (join / leave / form). Returns 53 on an emitted request, else 0.
int NpcAction9_EvaluateSocializeGroup(u8 prevResult, const u16* record, void* reqA,
                                      u8 relFlag, void* reqB);

// ---------------------------------------------------------------------------
// Pure deterministic cores (exposed for golden tests; no hooks).
// ---------------------------------------------------------------------------
// x87 (int)double truncation toward zero (Coord_ConvertX).
i32 NpcAction9_TruncToInt(double v);

// EvaluateShoot affordability gate: returns true if the combined-wealth estimate
//   (int)((wealthSelf + wealthTarget) * 0.005) exceeds the spend budget
//   (int)(currency * 0.22) — i.e. the "too expensive, bail" condition.
bool NpcAction9_ShootTooExpensive(int wealthSelf, int wealthTarget, int currency);

// EvaluateThrowAtRival bounty: (int) clamp((int)((favTerm)*0.01), 3200, 320000)*0.01,
//   then trunc, with favTerm = (200 - fav)*0.01 * playerHandlers * (wealth*0.005).
//   Returns the bounty (the v38 the original compares against currency*0.44).
int NpcAction9_RivalBounty(int wealth, float favorability, int playerHandlers);

// EvaluateArrest bounty: if wealth*0.01 <= 160 -> 160; else trunc(wealth*0.01).
int NpcAction9_ArrestBounty(int wealth);

// The jailed/recruit "loyalty roll" gate: byteAt89_top < RandomModulo(0x40)+37.
// (Exposed as the pure comparison given the two operands.)
bool NpcAction9_LoyaltyRollPasses(int loyaltyTopByte, int roll0to63);

} // namespace guild::sim
