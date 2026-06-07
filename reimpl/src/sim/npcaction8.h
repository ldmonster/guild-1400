#pragma once
// NpcAction8 — the AI "candidate-scoring / action-evaluation" leaf family of the
// NpcAction cluster (gilde.exe). Each of these is one option in the NPC's per-tick
// action table: given the actor's object/Person record and a per-action relation
// flag, it produces a pair of float scores (written through two out-pointers) and
// returns an action code (the dispatcher compares the scores to pick the winner).
//
// Two structural shapes recur:
//
//  (1) Score evaluators (MoveTo / MoveGuarded / MoveToSecondary / MoveToTertiary /
//      ApproachMarket / ApproachTavern / ApproachShop). They seed the two out
//      floats to a sentinel (0, -100, or -1e30), optionally gate on a record field
//      / a law record / a building-rank roll, then delegate to one of three shared
//      AiScore relation kernels (Distance, Weighted, Own) and sometimes post-scale
//      the result by a fixed float. The AiScore kernels and the law-record fetch
//      are cross-module leaves routed through NpcAction8Hooks.
//
//  (2) Search evaluators (IdleStand / CloseDoor / OpenDoorLarge / OpenDoorSmall).
//      They zero a 24-byte request block (+ a couple of param bytes for the door
//      variants), call a target-search dispatcher, and on success copy two 24-byte
//      coordinate blocks out and return a fixed action code; on failure return 0.
//
//  Plus two non-scoring leaves: AimTurretToward (a float-physics "aim/recoil
//  scatter" that, after a successful UseObjectAction, writes four global aim
//  accumulators from parallel direction tables) and RequestSellObjekt (emits a
//  market-sell command using the cached market price).
//
// Every cross-module callee (the AiScore kernels, the law-record fetch, the two
// search dispatchers, the economy demand snapshot, building category/rank lookups,
// the rival finder, RandomModulo, GameObject query, inventory/use-object, market
// price, the command queue) is routed through NpcAction8Hooks with inert defaults
// defined in npcaction8.cpp. Tests install a recording mock; nullptr restores the
// inert defaults. The pure deterministic cores (the post-scale float multiplies,
// the market gate, the recoil scatter math) are also exposed for golden tests.
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x4715e8 EvaluateMoveTo            0x471850 EvaluateMoveGuarded
//   0x471ce4 EvaluateMoveToSecondary   0x471e0c EvaluateMoveToTertiary
//   0x47156c EvaluateIdleStand         0x471f34 EvaluateCloseDoor
//   0x471d68 EvaluateOpenDoorLarge     0x471e90 EvaluateOpenDoorSmall
//   0x471fb0 EvaluateApproachMarket    0x473f8c EvaluateApproachTavern
//   0x4735f8 EvaluateApproachShop
//   0x470c00 AimTurretToward           0x471000 RequestSellObjekt
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Scratch blocks the search evaluators marshal. The original passes two 24-byte
// (0x18) blocks by reference and qmemcpy's the dispatcher's results out. We model
// them as opaque 24-byte buffers (the dispatcher fills the bytes).
// ===========================================================================
constexpr int kNpc8CoordBlockSize = 24;   // 0x18

// The three AiScore relation kernels share this small descriptor of the inputs the
// score evaluators feed them. The original calls take the two out-float pointers,
// the actor record pointer, the relation flag, and three trailing ints (a "kind"
// byte plus two context dwords). We collapse the trailing context to the values
// each caller actually passes; the kernel writes *outX/*outY and returns an int.
struct AiScoreCall {
    const void* record;   // actor object/Person record (ecx in the originals)
    u8          relFlag;  // the bl relation flag (a3)
    int         kind;     // the "kind" byte (0 for most callers)
    int         ctxA;     // trailing context dword a6
    int         ctxB;     // trailing context dword a7
};

// ===========================================================================
// Cross-module leaf hooks for the NpcAction8 family. Records are raw byte buffers,
// matching the originals' *(_TYPE *)(base + off). nullptr / 0 models an empty world.
// ===========================================================================
struct NpcAction8Hooks {
    // --- AiScore relation kernels (0x479a4c / 0x4796b0 / 0x479898). Each writes
    //     the two out floats and returns the int score code. The inert default
    //     leaves the outputs untouched and returns 0.
    int  (*scoreDistance)(float* outX, float* outY, const AiScoreCall& c);  // 0x479a4c
    int  (*scoreWeighted)(float* outX, float* outY, const AiScoreCall& c);  // 0x4796b0
    int  (*scoreOwn)(float* outX, float* outY, const AiScoreCall& c);       // 0x479898

    // VIBE_Gesetz_GetRecord(lawId, out): the score evaluators read out[+0x18]
    //   (the "use distance kernel" flag, v12) and pass `out` to scoreDistance.
    //   Returns the value of that flag (non-zero => use the distance kernel). The
    //   inert default returns 0 (=> weighted kernel) and zero-fills lawRec.
    int  (*getLawRecord)(int lawId, void* lawRecOut /* >=0x1c bytes */);

    // Target-search dispatchers. Fill the two 24-byte blocks (req in/out + result)
    //   and return non-zero on success. (0x47c430 primary, 0x47cc68 secondary.)
    int  (*dispatchPrimarySearch)(u8 flag, int actor, void* reqBlock /*24*/, void* resBlock /*24*/);
    int  (*dispatchSecondarySearch)(u8 flag, int actor, void* reqBlock /*24*/, void* resBlock /*24*/);

    // VIBE_Economy_LoadDemandSnapshot(out[13 floats]) — fills the demand snapshot;
    //   ApproachMarket reads out[3]. Inert default zero-fills.
    void (*loadDemandSnapshot)(float* out13);

    // VIBE_Building_GetCategoryForObject(record, kind) (0x589d24). Inert => 0.
    int  (*buildingCategoryForObject)(const void* record, int kind);
    // VIBE_BuildingType_ComputeRankWithinGroup(typeCode) (0x58a560). Inert => 0.
    int  (*buildingRankWithinGroup)(int typeCode);
    // VIBE_AiPlayer_FindRivalToConfront(record, outA[24], outB[24]) (0x47d970).
    //   Returns non-zero if a rival was found (and the blocks filled). Inert => 0.
    int  (*findRivalToConfront)(const void* record, void* outA, void* outB);

    // VIBE_Math_RandomModulo(n) (0x58b89c). Inert => 0.
    u16  (*randomModulo)(u16 n);

    // The global at 0x13CE852, WORD2(qword_13CE852) — a game-state word ApproachShop
    //   gates on ( >= 0x14 ). Inert default 0.
    int  (*shopStateWord)();

    // --- AimTurretToward leaves ---
    // VIBE_GameObject_QueryFind(slot, a, b, id) (0x5857fc) -> object record (the
    //   first word is the item id), or nullptr.
    const u16* (*gameObjectQueryFind)(int slot, int a, int b, int id);
    // VIBE_Inventory_FindSlotByItemId(itemId) (0x54f04c). Inert => 0 (not found).
    int  (*inventoryFindSlotByItemId)(int itemId);
    // VIBE_Item_UseObjectAction(actor, req3[3 words]) (0x5671f4): performs the use;
    //   returns 1 on success and writes a direction sign into *outDir (the v11 the
    //   original reads back, 1 = forward, -1 = reverse) and a "speed" record ptr
    //   into *outSpeedRec (the v7 base whose +12 float is the speed). Inert => 0.
    int  (*itemUseObjectAction)(int actor, const u16* req3, int* outDir, const void** outSpeedRec);

    // --- RequestSellObjekt leaves ---
    // VIBE_Building_LookupCachedMarketPrice(itemHiword, currencyByte) (0x58f6b8).
    double (*lookupCachedMarketPrice)(int itemHiword, u8 currency);
    // VIBE_Command_QueueRequest17(idA, idB, count, kind, currency, price) (0x49465c).
    void (*queueRequest17)(i32 idA, i32 idB, int count, int kind, u8 currency, long long price);

    // byte_6477A1 — the active player's currency byte (read by RequestSellObjekt).
    u8 currencyByte;
};

void SetNpcAction8Hooks(const NpcAction8Hooks* hooks);
const NpcAction8Hooks& GetNpcAction8Hooks();

// ===========================================================================
// Score evaluators. outX/outY receive the score pair; the return value is the
// action code (0 = "not applicable").
//
// MoveTo (0x4715e8): zero outs; Gesetz(18); if flag set use Distance else Weighted
//   (kind 0). It has no early-disable arg.
// MoveGuarded (0x471850, lawId 3), MoveToSecondary (0x471ce4, lawId 4),
//   MoveToTertiary (0x471e0c, lawId 21): if `disabled`, write -1e30 and return 0;
//   else same Gesetz+score dance.
// ===========================================================================
int NpcAction8_EvaluateMoveTo(float* outX, float* outY, u8 relFlag, int ctxA, int ctxB);
int NpcAction8_EvaluateMoveGuarded(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB);
int NpcAction8_EvaluateMoveToSecondary(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB);
int NpcAction8_EvaluateMoveToTertiary(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB);

// ===========================================================================
// Search evaluators. `outReq`/`outRes` receive the two 24-byte result blocks on
// success. Returns the action code or 0.
//
// IdleStand (0x47156c): if `prevResult` non-zero -> 0; if relFlag set -> prevResult;
//   else zero the request block, primary-search; on hit copy blocks, return 39.
// CloseDoor (0x471f34): same shape but secondary-search, returns 45.
// OpenDoorLarge (0x471d68) / OpenDoorSmall (0x471e90): if relFlag set -> 0; build a
//   request block ([0]=16, [1]=8 large / 4 small), secondary-search (flag=prevResult);
//   on hit copy blocks, return 43 / 44.
// ===========================================================================
int NpcAction8_EvaluateIdleStand(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes);
int NpcAction8_EvaluateCloseDoor(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes);
int NpcAction8_EvaluateOpenDoorLarge(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes);
int NpcAction8_EvaluateOpenDoorSmall(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes);

// ===========================================================================
// Economic approach evaluators.
//
// ApproachMarket (0x471fb0): record@ecx. If record[+358] != 15 -> -1e30, 0. Else
//   zero outs, load demand snapshot; if (record_price + (-2.0)) * 0.4 >= demand[3]
//   use Weighted (return its code); else Own then scale both outs by 3.0, return 1.
// ApproachTavern (0x473f8c): record@ecx. Seed outs to -100. If `disabled` -> -1e30,0.
//   Gate: record[+2]!=5 -> 0; record[+361]!=0 -> 0. rank = Rank(HIBYTE(record[+353])).
//   rank<3 -> 0; 3<=rank<6 and RandomModulo(rank-1)==0 -> 0. rank<6 => Weighted code;
//   else Own then scale outs by (2.0, 3.0), return 1.
// ApproachShop (0x4735f8): record@ecx. Seed outs to -1e30. If `disabled` -> 0.
//   cat = CategoryForObject(record,4). If cat && Rank(cat) > 2: FindRivalToConfront;
//   on hit Own + copy blocks + scale outs by (6.0, 2.0), return 1; else fallthrough 0.
//   Else if shopStateWord() >= 0x14 -> Weighted code; else 0.
// ===========================================================================
int NpcAction8_EvaluateApproachMarket(float* outX, float* outY, const void* record,
                                       u8 relFlag, int ctxA, int ctxB);
int NpcAction8_EvaluateApproachTavern(float* outX, float* outY, const void* record,
                                       u8 relFlag, bool disabled, int ctxA, int ctxB);
int NpcAction8_EvaluateApproachShop(float* outX, float* outY, const void* record,
                                     u8 relFlag, bool disabled, void* outBlkA, void* outBlkB,
                                     int ctxA, int ctxB);

// ===========================================================================
// AimTurretToward (0x470c00). actor@eax, target@edx, aimParams@ebx (a __int16*
//   array: [1],[2] copied into the use-request, [4] selects a direction-table row).
//   Returns 0 if the object/inventory/use checks fail; otherwise 36, having written
//   the four global aim accumulators. dir 1 => forward scatter; dir -1 => reverse,
//   negated and dampened by 0.5; any other dir => 36 with no scatter write.
//   The four accumulators are exposed for tests.
// ===========================================================================
int NpcAction8_AimTurretToward(int actor, int target, const i16* aimParams);

struct AimAccumulators { float x, y, z, w; };
AimAccumulators NpcAction8_GetAimAccumulators();
void NpcAction8_ResetAimAccumulators();

// The four direction tables (37-dword stride). Tests can install a table via this;
// nullptr restores the inert (all-zero) tables. base points at four float[] arrays
// laid out consecutively as the original (flt_B57244/4C/54/5C), each row stride 37.
void NpcAction8_SetAimTables(const float* tx, const float* ty, const float* tz,
                             const float* tw, int rowStride);

// ===========================================================================
// RequestSellObjekt (0x471000). actor@eax, descriptor@edx, target@ebx. If
//   descriptor[+0] != 2 -> 0. Else look up the cached market price (twice, exactly
//   as the original) and emit a QueueRequest17 sell command. Returns 12.
// ===========================================================================
int NpcAction8_RequestSellObjekt(const void* actor, const u8* descriptor, const void* target);

// ---------------------------------------------------------------------------
// Pure deterministic cores (exposed for golden tests; no hooks).
// ---------------------------------------------------------------------------
// The ApproachMarket gate: (price + (-2.0)) * 0.4 >= demand. Returns true if the
// "use Weighted kernel" branch is taken.
bool NpcAction8_MarketUseWeighted(float price, float demand);
// The post-scale applied to a score pair (multiply each by the fixed factor).
void NpcAction8_ScalePair(float* x, float* y, float fx, float fy);
// Recoil scatter: forward = tableVal * speed; reverse = -(tableVal * speed * 0.5).
float NpcAction8_ScatterForward(float tableVal, float speed);
float NpcAction8_ScatterReverse(float tableVal, float speed);

} // namespace guild::sim
