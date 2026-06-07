// NpcAction8 — AI candidate-scoring / action-evaluation leaf family. See
// npcaction8.h for the function map and calling-convention recovery.
//
// Calling convention (recovered from each prologue + the score-evaluator table):
//   * Score evaluators take the two out-float pointers in eax/edx, the relation
//     flag in bl, and (for the gated variants) an early-disable byte plus two
//     context dwords on the stack. They write the two scores and return the action
//     code via al/eax.
//   * Search evaluators take the previous result in al, the relation flag in bl,
//     the actor handle in edx, and two 24-byte block pointers (ecx + stack).
//   * The economic-approach evaluators take the actor object/Person record in ecx.
//   * AimTurretToward: actor@eax, target@edx, aimParams@ebx.
//   * RequestSellObjekt: actor@eax, descriptor@edx, target@ebx.
//
// Recovered constants (imagebase 0x400000):
//   ApproachMarket : dbl_61A538 = -2.0, dbl_61A540 = 0.4, flt_61A548 = 3.0
//   ApproachShop   : flt_61A5F8 = 6.0,  flt_61A5FC = 2.0
//   ApproachTavern : flt_61A624 = 2.0,  flt_61A628 = 3.0
//   AimTurret      : flt_61A4E8 = 0.5   (reverse-recoil dampening)
//   Direction-table row stride = 37 dwords (flt_B57244/4C/54/5C).
//
// Cross-module leaves are routed through NpcAction8Hooks with inert defaults; the
// float math is x87-equivalent (single precision tables, double intermediates for
// the market gate exactly as the fld/fmul/fcomp the original emits).

#include "sim/npcaction8.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook plumbing (mirrors the NpcAction7 pattern).
// ---------------------------------------------------------------------------
static const NpcAction8Hooks kInertHooks{};
static const NpcAction8Hooks* g_hooks8 = &kInertHooks;
void SetNpcAction8Hooks(const NpcAction8Hooks* hooks) {
    g_hooks8 = hooks ? hooks : &kInertHooks;
}
const NpcAction8Hooks& GetNpcAction8Hooks() { return *g_hooks8; }

// Recovered float constants.
static constexpr double kMarketPriceBias = -2.0;   // dbl_61A538
static constexpr double kMarketDemandMul = 0.4;    // dbl_61A540
static constexpr float  kMarketScale     = 3.0f;   // flt_61A548
static constexpr float  kShopScaleX      = 6.0f;   // flt_61A5F8
static constexpr float  kShopScaleY      = 2.0f;   // flt_61A5FC
static constexpr float  kTavernScaleX    = 2.0f;   // flt_61A624
static constexpr float  kTavernScaleY    = 3.0f;   // flt_61A628
static constexpr float  kReverseDamp     = 0.5f;   // flt_61A4E8

static constexpr float  kDisableSentinel = -1.0e30f;

// ---------------------------------------------------------------------------
// Pure deterministic cores.
// ---------------------------------------------------------------------------
bool NpcAction8_MarketUseWeighted(float price, float demand) {
    // (price + (-2.0)) * 0.4 >= demand  — done in double, matching the x87 chain.
    return (static_cast<double>(price) + kMarketPriceBias) * kMarketDemandMul
           >= static_cast<double>(demand);
}

void NpcAction8_ScalePair(float* x, float* y, float fx, float fy) {
    *x = *x * fx;
    *y = *y * fy;
}

float NpcAction8_ScatterForward(float tableVal, float speed) {
    return tableVal * speed;
}
float NpcAction8_ScatterReverse(float tableVal, float speed) {
    return -(tableVal * speed * kReverseDamp);
}

// ===========================================================================
// Score evaluators.
// ===========================================================================

// The shared "fetch law record, then dispatch to Distance/Weighted" tail. lawId is
// the Gesetz id; `kind` is the trailing kind byte the Weighted kernel receives.
static int ScoreViaLaw(float* outX, float* outY, u8 relFlag, int lawId, int kind,
                       int ctxA, int ctxB) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    unsigned char lawRec[32];
    std::memset(lawRec, 0, sizeof(lawRec));
    int useDistance = hk.getLawRecord ? hk.getLawRecord(lawId, lawRec) : 0;  // v12
    AiScoreCall c{lawRec, relFlag, kind, ctxA, ctxB};
    if (useDistance) {
        return hk.scoreDistance ? hk.scoreDistance(outX, outY, c) : 0;
    }
    return hk.scoreWeighted ? hk.scoreWeighted(outX, outY, c) : 0;
}

// gilde.exe 0x4715e8 — VIBE_NpcAction_EvaluateMoveTo (eax=outX, edx=outY, bl=flag).
//   No early-disable arg. The Weighted call passes an uninitialized kind byte in
//   the original (a decompiler artifact for the missing 5th arg); the kernel's
//   meaningful input is the relation flag, so we feed kind 0.
int NpcAction8_EvaluateMoveTo(float* outX, float* outY, u8 relFlag, int ctxA, int ctxB) {
    *outX = 0.0f;
    *outY = 0.0f;
    return ScoreViaLaw(outX, outY, relFlag, /*lawId=*/18, /*kind=*/0, ctxA, ctxB);
}

// gilde.exe 0x471850 / 0x471ce4 / 0x471e0c — the disable-gated MoveTo variants.
static int EvaluateGuardedMove(float* outX, float* outY, u8 relFlag, bool disabled,
                               int lawId, int ctxA, int ctxB) {
    if (disabled) {
        *outX = kDisableSentinel;
        *outY = kDisableSentinel;
        return 0;
    }
    *outX = 0.0f;
    *outY = 0.0f;
    return ScoreViaLaw(outX, outY, relFlag, lawId, /*kind=*/0, ctxA, ctxB);
}
int NpcAction8_EvaluateMoveGuarded(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB) {
    return EvaluateGuardedMove(outX, outY, relFlag, disabled, /*lawId=*/3, ctxA, ctxB);
}
int NpcAction8_EvaluateMoveToSecondary(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB) {
    return EvaluateGuardedMove(outX, outY, relFlag, disabled, /*lawId=*/4, ctxA, ctxB);
}
int NpcAction8_EvaluateMoveToTertiary(float* outX, float* outY, u8 relFlag, bool disabled, int ctxA, int ctxB) {
    return EvaluateGuardedMove(outX, outY, relFlag, disabled, /*lawId=*/21, ctxA, ctxB);
}

// ===========================================================================
// Search evaluators.
// ===========================================================================

// gilde.exe 0x47156c — VIBE_NpcAction_EvaluateIdleStand. Primary search, code 39.
int NpcAction8_EvaluateIdleStand(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes) {
    if (prevResult) return 0;
    if (relFlag) return prevResult;        // (relFlag set => keep prior result, i.e. 0)
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    unsigned char req[kNpc8CoordBlockSize]; std::memset(req, 0, sizeof(req));
    unsigned char res[kNpc8CoordBlockSize]; std::memset(res, 0, sizeof(res));
    // The original zeroes only req[0]=0 (byte), req[1]=0, req[2]=0 (dwords); a full
    // zero-fill is behaviorally identical for the dispatcher contract.
    if (hk.dispatchPrimarySearch && hk.dispatchPrimarySearch(0, actor, req, res)) {
        std::memcpy(outReq, req, kNpc8CoordBlockSize);
        std::memcpy(outRes, res, kNpc8CoordBlockSize);
        return 39;
    }
    return 0;
}

// gilde.exe 0x471f34 — VIBE_NpcAction_EvaluateCloseDoor. Secondary search, code 45.
int NpcAction8_EvaluateCloseDoor(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes) {
    if (prevResult) return 0;
    if (relFlag) return prevResult;
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    unsigned char req[kNpc8CoordBlockSize]; std::memset(req, 0, sizeof(req));
    unsigned char res[kNpc8CoordBlockSize]; std::memset(res, 0, sizeof(res));
    if (hk.dispatchSecondarySearch && hk.dispatchSecondarySearch(0, actor, req, res)) {
        std::memcpy(outReq, req, kNpc8CoordBlockSize);
        std::memcpy(outRes, res, kNpc8CoordBlockSize);
        return 45;
    }
    return 0;
}

// gilde.exe 0x471d68 / 0x471e90 — the door evaluators. Request block carries
//   [+0](byte)=16 and [+4](dword)=8 (large) / 4 (small); the dispatch flag is the
//   prior result. Returns 43 (large) / 44 (small).
static int EvaluateOpenDoor(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes,
                            int sizeParam, int code) {
    if (relFlag) return 0;
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    unsigned char req[kNpc8CoordBlockSize]; std::memset(req, 0, sizeof(req));
    unsigned char res[kNpc8CoordBlockSize]; std::memset(res, 0, sizeof(res));
    // VIBE_Light_SetGrayColorThunk(0,24) is called twice for side effects only;
    // it does not touch the request block, so it is elided (no observable result).
    req[0] = 16;                                          // LOBYTE(v10[0]) = 16
    std::memcpy(req + 4, &sizeParam, sizeof(int));        // v10[1] = 8 / 4
    if (hk.dispatchSecondarySearch && hk.dispatchSecondarySearch(prevResult, actor, req, res)) {
        std::memcpy(outReq, req, kNpc8CoordBlockSize);
        std::memcpy(outRes, res, kNpc8CoordBlockSize);
        return code;
    }
    return 0;
}
int NpcAction8_EvaluateOpenDoorLarge(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes) {
    return EvaluateOpenDoor(prevResult, relFlag, actor, outReq, outRes, /*sizeParam=*/8, /*code=*/43);
}
int NpcAction8_EvaluateOpenDoorSmall(u8 prevResult, u8 relFlag, int actor, void* outReq, void* outRes) {
    return EvaluateOpenDoor(prevResult, relFlag, actor, outReq, outRes, /*sizeParam=*/4, /*code=*/44);
}

// ===========================================================================
// Economic approach evaluators.
// ===========================================================================

static const u8* AsBytes(const void* p) { return static_cast<const u8*>(p); }

// gilde.exe 0x471fb0 — VIBE_NpcAction_EvaluateApproachMarket.
int NpcAction8_EvaluateApproachMarket(float* outX, float* outY, const void* record,
                                       u8 relFlag, int ctxA, int ctxB) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    if (AsBytes(record)[358] != 15) {
        *outX = kDisableSentinel;
        *outY = kDisableSentinel;
        return 0;
    }
    *outX = 0.0f;
    *outY = 0.0f;
    float demand[13]; std::memset(demand, 0, sizeof(demand));
    if (hk.loadDemandSnapshot) hk.loadDemandSnapshot(demand);
    float price = *reinterpret_cast<const float*>(AsBytes(record) + 256);
    AiScoreCall c{record, relFlag, /*kind=*/0, ctxA, ctxB};
    if (NpcAction8_MarketUseWeighted(price, demand[3])) {
        return hk.scoreWeighted ? hk.scoreWeighted(outX, outY, c) : 0;
    }
    if (hk.scoreOwn) hk.scoreOwn(outX, outY, c);
    NpcAction8_ScalePair(outX, outY, kMarketScale, kMarketScale);
    return 1;
}

// gilde.exe 0x473f8c — VIBE_NpcAction_EvaluateApproachTavern.
int NpcAction8_EvaluateApproachTavern(float* outX, float* outY, const void* record,
                                       u8 relFlag, bool disabled, int ctxA, int ctxB) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    *outX = -100.0f;
    *outY = -100.0f;
    if (disabled) {
        *outX = kDisableSentinel;
        *outY = kDisableSentinel;
        return 0;
    }
    const u8* rec = AsBytes(record);
    if (rec[2] != 5) return 0;
    if (rec[361] != 0) return 0;
    // rank = Rank(HIBYTE(*(dword*)(record+353)))  == byte at record+356.
    int typeCode = rec[356];
    int rank = hk.buildingRankWithinGroup ? hk.buildingRankWithinGroup(typeCode) : 0;
    if (rank < 3) return 0;
    if (rank < 6) {
        u16 roll = hk.randomModulo ? hk.randomModulo(static_cast<u16>(rank - 1)) : 0;
        if (roll == 0) return 0;
    }
    AiScoreCall c{record, relFlag, /*kind=*/0, ctxA, ctxB};
    if (rank < 6) {
        return hk.scoreWeighted ? hk.scoreWeighted(outX, outY, c) : 0;
    }
    if (hk.scoreOwn) hk.scoreOwn(outX, outY, c);
    NpcAction8_ScalePair(outX, outY, kTavernScaleX, kTavernScaleY);
    return 1;
}

// gilde.exe 0x4735f8 — VIBE_NpcAction_EvaluateApproachShop.
int NpcAction8_EvaluateApproachShop(float* outX, float* outY, const void* record,
                                     u8 relFlag, bool disabled, void* outBlkA, void* outBlkB,
                                     int ctxA, int ctxB) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    *outX = kDisableSentinel;
    *outY = kDisableSentinel;
    if (disabled) return 0;
    int cat = hk.buildingCategoryForObject ? hk.buildingCategoryForObject(record, 4) : 0;
    AiScoreCall c{record, relFlag, /*kind=*/0, ctxA, ctxB};
    if (cat && (hk.buildingRankWithinGroup ? hk.buildingRankWithinGroup(cat) : 0) > 2) {
        unsigned char blkA[kNpc8CoordBlockSize]; std::memset(blkA, 0, sizeof(blkA));
        unsigned char blkB[kNpc8CoordBlockSize]; std::memset(blkB, 0, sizeof(blkB));
        if (hk.findRivalToConfront && hk.findRivalToConfront(record, blkA, blkB)) {
            if (hk.scoreOwn) hk.scoreOwn(outX, outY, c);
            std::memcpy(outBlkA, blkA, kNpc8CoordBlockSize);
            std::memcpy(outBlkB, blkB, kNpc8CoordBlockSize);
            NpcAction8_ScalePair(outX, outY, kShopScaleX, kShopScaleY);
            return 1;
        }
        return 0;   // rival path taken but no rival found
    }
    // The non-category branch: gate on the shop state word >= 0x14.
    if ((hk.shopStateWord ? hk.shopStateWord() : 0) >= 0x14) {
        return hk.scoreWeighted ? hk.scoreWeighted(outX, outY, c) : 0;
    }
    return 0;
}

// ===========================================================================
// AimTurretToward (0x470c00).
// ===========================================================================
static AimAccumulators g_aim{0, 0, 0, 0};   // flt_B58734 / 3C / 44 / 4C

// Direction tables (37-dword stride). Inert default => all-zero rows.
static const float* g_tx = nullptr;
static const float* g_ty = nullptr;
static const float* g_tz = nullptr;
static const float* g_tw = nullptr;
static int g_aimStride = 37;

void NpcAction8_SetAimTables(const float* tx, const float* ty, const float* tz,
                             const float* tw, int rowStride) {
    g_tx = tx; g_ty = ty; g_tz = tz; g_tw = tw;
    g_aimStride = rowStride > 0 ? rowStride : 37;
}
AimAccumulators NpcAction8_GetAimAccumulators() { return g_aim; }
void NpcAction8_ResetAimAccumulators() { g_aim = AimAccumulators{0, 0, 0, 0}; }

static float TableRow(const float* tbl, int row) {
    return tbl ? tbl[g_aimStride * row] : 0.0f;
}

int NpcAction8_AimTurretToward(int actor, int target, const i16* aimParams) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    // v4 = GameObject_QueryFind(*(actor+376), 1, 1, *(target+4)); the actor/target
    //   field reads are part of the hook contract, so we forward the raw handles
    //   and let the hook deref. We pass actor as the "slot owner" and the two ids.
    const u16* obj = hk.gameObjectQueryFind
        ? hk.gameObjectQueryFind(actor, 1, 1, target)
        : nullptr;
    if (!obj) return 0;
    if (!(hk.inventoryFindSlotByItemId ? hk.inventoryFindSlotByItemId(*obj) : 0))
        return 0;

    // Marshal the 3-word use request: v10[0]=<obj id mirror>, v10[1]=aimParams[1],
    //   v10[2]=aimParams[2].
    u16 req3[3];
    req3[0] = *obj;
    req3[1] = static_cast<u16>(aimParams[1]);
    req3[2] = static_cast<u16>(aimParams[2]);

    int dir = 0;
    const void* speedRec = nullptr;
    int used = hk.itemUseObjectAction
        ? hk.itemUseObjectAction(actor, req3, &dir, &speedRec)
        : 0;
    if (used != 1) return 0;

    int row = aimParams[4];
    float speed = speedRec ? *reinterpret_cast<const float*>(AsBytes(speedRec) + 12) : 0.0f;

    if (dir == 1) {
        g_aim.x = NpcAction8_ScatterForward(TableRow(g_tx, row), speed);
        g_aim.y = NpcAction8_ScatterForward(TableRow(g_ty, row), speed);
        g_aim.z = NpcAction8_ScatterForward(TableRow(g_tz, row), speed);
        g_aim.w = NpcAction8_ScatterForward(TableRow(g_tw, row), speed);
        return 36;
    }
    if (dir != -1) return 36;
    g_aim.x = NpcAction8_ScatterReverse(TableRow(g_tx, row), speed);
    g_aim.y = NpcAction8_ScatterReverse(TableRow(g_ty, row), speed);
    g_aim.z = NpcAction8_ScatterReverse(TableRow(g_tz, row), speed);
    g_aim.w = NpcAction8_ScatterReverse(TableRow(g_tw, row), speed);
    return 36;
}

// ===========================================================================
// RequestSellObjekt (0x471000).
// ===========================================================================
int NpcAction8_RequestSellObjekt(const void* actor, const u8* descriptor, const void* target) {
    const NpcAction8Hooks& hk = GetNpcAction8Hooks();
    if (descriptor[0] != 2) return 0;

    // itemHiword = HIWORD(*(dword*)(descriptor + 2)).
    int descDword = *reinterpret_cast<const int*>(descriptor + 2);
    int itemHiword = (descDword >> 16) & 0xFFFF;

    // The original looks up the price twice (the sprintf debug line and the command
    //   each recompute it); we mirror that exactly even though the inputs match.
    if (hk.lookupCachedMarketPrice)
        hk.lookupCachedMarketPrice(itemHiword, hk.currencyByte);   // debug-line price
    double price = hk.lookupCachedMarketPrice
        ? hk.lookupCachedMarketPrice(itemHiword, hk.currencyByte)
        : 0.0;

    i32 actorId  = *reinterpret_cast<const i32*>(AsBytes(actor) + 4);
    i32 targetId = *reinterpret_cast<const i32*>(AsBytes(target) + 4);
    // count = *(dword*)(v9+8), kind = HIWORD(*(dword*)(v9+2)) — both off the object
    //   record the use lookup resolves; we read them off the descriptor block, which
    //   is what v9 aliases here.
    int count = *reinterpret_cast<const int*>(descriptor + 8);
    int kind  = itemHiword;

    if (hk.queueRequest17)
        hk.queueRequest17(actorId, targetId, count, kind, hk.currencyByte,
                          static_cast<long long>(price));
    return 12;
}

} // namespace guild::sim
