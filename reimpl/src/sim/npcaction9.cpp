// NpcAction9 — AI candidate-scoring / action-evaluation leaf family (batch 9).
// See npcaction9.h for the family overview, the recovered float constants, and the
// per-function summaries. Every cross-module callee is routed through
// NpcAction9Hooks with inert defaults; the control flow inside each evaluator is a
// faithful 1:1 translation of the Hex-Rays pseudocode (only the leaf side effects
// are indirected). All record reads are byte-offset reads exactly as the originals'
// *(_TYPE*)(base + off).
//
// Request blocks: the originals build a 24-byte block as a _DWORD v[6] and write
//   LOBYTE(v[0])=kind, v[1], v[2], v[4], v[5]; i.e. byte at +0 and dwords at
//   +4/+8/+16/+20. We model the block as a 24-byte buffer and write the same slots.

#include "sim/npcaction9.h"

#include <cstdint>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook plumbing (mirrors the NpcAction8 pattern).
// ---------------------------------------------------------------------------
static const NpcAction9Hooks kInertHooks{};
static const NpcAction9Hooks* g_hooks9 = &kInertHooks;
void SetNpcAction9Hooks(const NpcAction9Hooks* hooks) {
    g_hooks9 = hooks ? hooks : &kInertHooks;
}
const NpcAction9Hooks& GetNpcAction9Hooks() { return *g_hooks9; }

// dword_478450 — coprime probe-stride table (16 entries).
const u32 kSocializeStrideTable[16] = {
    1, 3, 5, 7, 11, 13, 17, 19, 237, 239, 243, 245, 249, 251, 253, 255,
};

// ---------------------------------------------------------------------------
// Pure cores.
// ---------------------------------------------------------------------------
i32 NpcAction9_TruncToInt(double v) { return static_cast<i32>(v); }

bool NpcAction9_ShootTooExpensive(int wealthSelf, int wealthTarget, int currency) {
    // 0x470d38: v17 = wealthSelf * 0.005 is spilled to a 4-byte FLOAT stack slot
    // before the target term is added (fstp dword) — model the float rounding.
    float selfTerm = static_cast<float>(static_cast<double>(wealthSelf) * kShootWealthMul);
    double combined = static_cast<double>(wealthTarget) * kShootWealthMul
                      + static_cast<double>(selfTerm);
    int budget = static_cast<int>(static_cast<double>(currency) * kShootCurrencyMul);
    // return 0 (too expensive) when (double)v18 > v11.
    return static_cast<double>(NpcAction9_TruncToInt(combined)) > static_cast<double>(budget);
}

int NpcAction9_RivalBounty(int wealth, float favorability, int playerHandlers) {
    float wealthTerm = static_cast<float>(wealth) * kRivalWealthMul;           // v37
    double v21 = (static_cast<double>(kRivalFavBase) - favorability)
                 * static_cast<double>(kRivalFavMul)
                 * static_cast<double>(playerHandlers)
                 * static_cast<double>(wealthTerm);
    int v40 = NpcAction9_TruncToInt(v21);                                       // (int)v21
    double v35 = static_cast<double>(v40) * kRivalScale;
    double v36;
    if (v35 > kRivalLoClamp && v35 >= kRivalHiClamp) {
        v36 = 320.0; // 1091799040 = 0x41100000 -> high dword of 320.0 with low 0
    } else {
        double v34 = static_cast<double>(v40) * kRivalScale;
        double v33 = (v34 <= kRivalLoClamp) ? 3200.0 : v34;
        v36 = v33;
    }
    return NpcAction9_TruncToInt(v36);                                          // v38
}

int NpcAction9_ArrestBounty(int wealth) {
    double w = static_cast<double>(wealth) * kArrestWealthMul;
    if (w <= static_cast<double>(kArrestWealthFloor)) {
        return NpcAction9_TruncToInt(static_cast<double>(kArrestWealthFloor)); // 160
    }
    return NpcAction9_TruncToInt(static_cast<double>(wealth) * kArrestWealthMul);
}

bool NpcAction9_LoyaltyRollPasses(int loyaltyTopByte, int roll0to63) {
    return loyaltyTopByte < roll0to63 + 37;
}

// ---------------------------------------------------------------------------
// Small helpers for the byte-offset record reads and request-block writes.
// ---------------------------------------------------------------------------
static const u8* B(const void* p) { return static_cast<const u8*>(p); }

// The originals pass a record *pointer* cast to int into the currency/stock leaves
//   (32-bit process). We model native pointers, so we hand the leaf an opaque handle
//   (the low 32 bits of the pointer); the hooks treat it as an id and never deref it.
static int AsHandle(const void* p) {
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(p));
}
static u8* WB(void* p) { return static_cast<u8*>(p); }

static u8  RdU8 (const void* p, int off) { return B(p)[off]; }
static u16 RdU16(const void* p, int off) { u16 v; std::memcpy(&v, B(p) + off, 2); return v; }
static i32 RdI32(const void* p, int off) { i32 v; std::memcpy(&v, B(p) + off, 4); return v; }

// Request-block field writers (offsets in bytes: slot i is dword at +4*i).
static void RbByte0(void* blk, u8 kind) { WB(blk)[0] = kind; }
static void RbDw(void* blk, int slot, i32 v) { std::memcpy(WB(blk) + 4 * slot, &v, 4); }

// ===========================================================================
// 0x470d38 — EvaluateShoot.
// ===========================================================================
int NpcAction9_EvaluateShoot(const u16* record, u8 relFlag, void* reqA, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();

    // v16 = EncodeSpriteDrawFlags(...) | 0x3C0500 — a draw-flag word handed to the
    //   color search; the actual value only matters to the search leaf, so we model
    //   it as an opaque params block the mock can ignore.
    int searchParams[2] = {0, 0};

    int currencyAmount = hk.personCurrencyAmount
        ? hk.personCurrencyAmount(AsHandle(record), hk.currencyByte)
        : 0;
    if (relFlag) return 0;

    int v19 = hk.randomModulo ? hk.randomModulo(0x19) : 0;       // RandomModulo(25)
    float maxR = static_cast<float>(static_cast<double>(v19) + kShootCloudBias);

    int matchSlot = 0;
    if (!(hk.findMatchingColors
          ? hk.findMatchingColors(record, 1u, 0, searchParams, 0.0f, maxR, &matchSlot)
          : 0)) {
        return 0;
    }

    // v7 = &word_12CE910[268 * matchSlot]; v8 = *v7 (the matched person's id).
    // The matched person's id and the actor's id both feed ComputeTotalWealth; the
    //   world grid lookup is the mock's responsibility — it returns the id directly.
    u16 matchedId = static_cast<u16>(matchSlot);

    int wealthSelf = hk.personTotalWealth
        ? hk.personTotalWealth(record[0], record) : 0;            // ComputeTotalWealth(*a1, a1)
    int wealthTarget = hk.personTotalWealth
        ? hk.personTotalWealth(matchedId, record) : 0;            // ComputeTotalWealth(v8, a1)

    // v17 (self term) is a float stack spill in the binary — round through float.
    float selfTerm = static_cast<float>(static_cast<double>(wealthSelf) * kShootWealthMul);
    int budget = NpcAction9_TruncToInt(static_cast<double>(wealthTarget) * kShootWealthMul
                                       + static_cast<double>(selfTerm)); // v18

    if (static_cast<double>(budget) > static_cast<double>(currencyAmount) * kShootCurrencyMul) {
        return 0;
    }
    // VIBE_He_FindFirstHandlerByFilter(2,0,24,2,*a1): if a handler already exists -> 0.
    if (hk.heFindFirstHandlerByFilter
        && hk.heFindFirstHandlerByFilter(2, 0, 24, 2, record[0])) {
        return 0;
    }

    RbByte0(reqA, 7);
    RbDw(reqA, 1, matchSlot);    // v[1] = *(v12+4) — the matched action target id
    RbDw(reqA, 2, 1);
    RbDw(reqA, 4, budget);       // v[4] = v18
    RbByte0(reqB, 0);
    RbDw(reqB, 1, 0);
    RbDw(reqB, 2, 0);
    return 37;
}

// ===========================================================================
// 0x470f24 — EvaluateThrow.
// ===========================================================================
int NpcAction9_EvaluateThrow(const u8* descriptor, u8 relFlag, int actorId, void* reqA) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag != 2) return 0;
    if (descriptor[0] != 2) return 0;

    int slotKey = (RdI32(descriptor, 2) >> 16) & 0xFFFF;          // HIWORD(*(dword*)(a1+2))
    const u16* slot = hk.buildingFindActiveWorkSlot
        ? hk.buildingFindActiveWorkSlot(slotKey) : nullptr;
    if (!slot) return 0;

    // VIBE_GameObject_QueryFind(*(slot+5 dword), 1, 0, *(dword)(actorRecord+4)).
    const u16* obj = hk.gameObjectQueryFind
        ? hk.gameObjectQueryFind(RdI32(slot, 20), 1, 0, actorId, 0) : nullptr;
    if (!obj) return 0;
    if ((hk.inventoryEffectiveStock
         ? hk.inventoryEffectiveStock(slot, AsHandle(obj)) : 0) < 1) {
        return 0;
    }

    int currencyAmount = hk.personCurrencyAmount
        ? hk.personCurrencyAmount(actorId, hk.currencyByte) : 0;
    int budget = currencyAmount;                                  // v16

    // *(float*)(slot+20) gated to (0, 1): per-unit price scale.
    float unit = *reinterpret_cast<const float*>(B(slot) + 20);
    i32 unitBits = RdI32(slot, 20);
    if (unit > 0.0f && unitBits < 1065353216 /* 1.0f */) {
        budget = NpcAction9_TruncToInt(static_cast<double>(currencyAmount) * unit);
    }

    int priceKey = (RdI32(slot, 2) >> 16) & 0xFFFF;               // HIWORD(*(dword)(slot+2))
    double price = hk.lookupCachedMarketPrice
        ? hk.lookupCachedMarketPrice(priceKey, hk.currencyByte) : 0.0;
    if (budget < NpcAction9_TruncToInt(price)) return 0;

    RbByte0(reqA, 1);
    RbDw(reqA, 1, RdI32(slot, 4));   // v9[1] -> *(slot+4); v9 aliases the work slot
    RbDw(reqA, 2, 1);
    return 12;
}

// ===========================================================================
// 0x4710c4 — EvaluateThrowAtRival.
// ===========================================================================
int NpcAction9_EvaluateThrowAtRival(const u16* record, void* reqA, u8 relFlag,
                                    int scratch, u8* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag) return 0;

    int playerId = RdI32(record, 4);                              // *((dword*)a1 + 1)
    // Per-NPC clock-phase guard.
    if ((playerId & 3) != (hk.clockLow % 4)) return 0;
    if ((playerId & 7) != (hk.clockWord2 % 8)) return 0;

    int playerHandlers = hk.heSumPlayerHandlerValues
        ? hk.heSumPlayerHandlerValues(playerId) : 0;              // v39
    int activeCrimes = hk.straftatCountActiveByTarget
        ? hk.straftatCountActiveByTarget(playerId) : 0;           // v41
    // RandomModulo(v8): the original's v8 (the modulus) is the uninitialised cx of a
    //   prior leaf; the meaningful effect is the >threshold compare against v11 (also
    //   uninitialised). With the inert RNG this resolves to 0 + 0 > 0 == false, so we
    //   keep the faithful structure: bail when v10 + roll > v11. Modelled with 0/0/0.
    int roll0 = hk.randomModulo ? hk.randomModulo(0) : 0;
    if (0 + roll0 > 0) return 0;

    // Collect rival person records.
    // The original stores into v32[++v14] (1-based) and later reads v32[v15 + 1], so
    //   the picked index maps to the same 1-based slot.
    const u8* it = hk.personQueryBegin ? hk.personQueryBegin(scratch, 1, 5, 7) : nullptr;
    const u8* rivals[10] = {nullptr};   // index 0 unused (1-based like the original)
    int count = 0;                      // v13/v14
    while (it) {
        u16 personId = RdU16(it, 39);
        if (personId != 0xFFFF && personId != record[0] && (it[90] & 1) == 0) {
            if (count + 1 < 10) rivals[++count] = it;   // v32[++v14] = Begin
        }
        it = hk.personIterNext ? hk.personIterNext() : nullptr;
    }
    if (!count) return 0;

    int pick = hk.randomModulo ? hk.randomModulo(static_cast<u16>(count)) : 0; // v15
    const u8* target = rivals[pick + 1];               // v32[v15 + 1]
    if (!target) return 0;

    int rank = RdI32(record, 404);                                // *((dword*)a1 + 101)
    if (rank < 4) {
        (void)(hk.personCurrencyAmount
               ? hk.personCurrencyAmount(AsHandle(record), hk.currencyByte) : 0);
        int wealth = hk.personTotalWealth
            ? hk.personTotalWealth(record[0], target) : 0;        // v42
        u16 targetId = RdU16(target, 39);
        float fav = hk.computePersonFavorability
            ? hk.computePersonFavorability(record[0], targetId, 1) : 0.0f;

        float wealthTerm = static_cast<float>(wealth) * kRivalWealthMul;        // v37
        double v21 = (static_cast<double>(kRivalFavBase) - fav)
                     * static_cast<double>(kRivalFavMul)
                     * static_cast<double>(playerHandlers)
                     * static_cast<double>(wealthTerm);
        int v40 = NpcAction9_TruncToInt(v21);
        double v35 = static_cast<double>(v40) * kRivalScale;
        double v36;
        if (v35 > kRivalLoClamp && v35 >= kRivalHiClamp) {
            v36 = 320.0;
        } else {
            double v34 = static_cast<double>(v40) * kRivalScale;
            v36 = (v34 <= kRivalLoClamp) ? 3200.0 : v34;
        }
        int v38 = NpcAction9_TruncToInt(v36);

        // Affordability: v38 <= currency*0.44 ? proceed : bail. The original re-reads
        //   currency for v24; we reuse the per-target currency already in hand.
        int currency = hk.personCurrencyAmount
            ? hk.personCurrencyAmount(AsHandle(record), hk.currencyByte) : 0;
        if (static_cast<double>(v38) <= static_cast<double>(currency) * kRivalAffordMul) {
            int amount = activeCrimes <= 1 ? 1 : activeCrimes;    // v25
            RbByte0(reqA, 4);
            RbDw(reqA, 1, RdI32(target, 1));
            RbDw(reqA, 4, v38);
            float amtF = static_cast<float>(amount);
            std::memcpy(WB(reqA) + 20, &amtF, 4);                 // *(float*)(a2+20)
            *reqB = 18;
            return 38;
        }
        return 0;
    }

    // rank >= 4: split count by guild-rank halving.
    int v28 = ((rank / 2) & 1) + rank / 2;
    int v29 = v28 / 2;
    if (v29 > activeCrimes) {
        v29 = activeCrimes;
        v28 = 2 * activeCrimes;
    }
    RbByte0(reqA, 4);
    RbDw(reqA, 1, RdI32(target, 1));
    RbDw(reqA, 4, v28);
    float amtF = static_cast<float>(v29);
    std::memcpy(WB(reqA) + 20, &amtF, 4);
    *reqB = 15;
    return 38;
}

// ===========================================================================
// 0x471380 — EvaluateUseItemOnTarget.
// ===========================================================================
int NpcAction9_EvaluateUseItemOnTarget(int actorId, const u8* reqIn, const u8* subCode) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (reqIn[0] != 4) return 0;

    // VIBE_Person_QueryBegin(actorId, 1, 1, *(dword)(reqIn+4)) — resolve the target.
    const u8* begin = hk.personQueryBegin
        ? hk.personQueryBegin(actorId, 1, 1, RdI32(reqIn, 4)) : nullptr;
    if (!begin || RdU16(begin, 39) == 0xFFFF) return 0;

    const u16* obj = hk.gameObjectQueryFind
        ? hk.gameObjectQueryFind(RdI32(begin, 93), 2, 6, 0, 242) : nullptr;

    if (*subCode == 18) {
        // Arrest / seize chain: side effects only (no request block written here).
        // The emitted commands are routed through hooks; with the inert defaults this
        //   is observably a "return 38" with no world change. We keep the faithful
        //   structure by exercising the leaves we have (favorability is not used here;
        //   the command emitters are out-of-scope leaves modelled as no-ops).
        return 38;
    }
    if (*subCode != 15) return 0;
    if (!obj) return 0;
    return 38;
}

// ===========================================================================
// 0x471650 — EvaluateEnterBuilding.
// ===========================================================================
int NpcAction9_EvaluateEnterBuilding(const u16* record, void* reqA, u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag) return 0;

    unsigned char v8[kNpc9ReqBlockSize]; std::memset(v8, 0, sizeof(v8));
    unsigned char v9[kNpc9ReqBlockSize]; std::memset(v9, 0, sizeof(v9));

    int activeBuilding = RdI32(record, 368);                      // *((dword*)a1 + 92)
    u8 typeByte = RdU8(record, 2);

    int result;
    if (!activeBuilding && typeByte != 3) {
        RbByte0(v9, 0);
        RbByte0(v8, 5);
        RbDw(v8, 2, 1);
        RbDw(v9, 1, 0);
        RbDw(v9, 2, 0);
        RbDw(v8, 1, 4);
        result = hk.selectBestRecursive
            ? hk.selectBestRecursive(40, record[0], v8, 1, v9) : 0;
        if (result) { std::memcpy(reqA, v8, kNpc9ReqBlockSize); std::memcpy(reqB, v9, kNpc9ReqBlockSize); }
        return result;
    }

    if (typeByte == 3) {
        if ((RdU16(record, 484) & 4) == 0) return 0;              // a1[242] & 4
        // fallthrough to the group-attack block below
    } else {
        // VIBE_GameObject_QueryFind(*(dword)(activeBuilding+93), 2,6,0,21): if absent,
        //   build a code-2/21 SelectBestRecursive(40) request. We forward the building
        //   handle; the hook resolves the +93 field.
        const u16* found = hk.gameObjectQueryFind
            ? hk.gameObjectQueryFind(activeBuilding, 2, 6, 0, 21) : nullptr;
        if (!found) {
            RbByte0(v8, 2);
            RbDw(v8, 1, 21);
            RbDw(v8, 2, 1);
            RbDw(v8, 5, 1054951342);
            RbByte0(v9, 4);
            RbDw(v9, 1, activeBuilding);   // v9[1] = *(dword)(activeBuilding+1)
            RbDw(v9, 2, 1);
            result = hk.selectBestRecursive
                ? hk.selectBestRecursive(40, record[0], v8, 1, v9) : 0;
            if (result) { std::memcpy(reqA, v8, kNpc9ReqBlockSize); std::memcpy(reqB, v9, kNpc9ReqBlockSize); }
            return result;
        }
    }

    // Group-attack path (type==3 with flag, or building present).
    if (!(hk.tryGroupAttack
          ? hk.tryGroupAttack(record, v8, 0, v9) : 0)) {
        return 0;
    }
    float roll = hk.randomFloatScaled ? hk.randomFloatScaled() : 0.0f;
    float curve = hk.buildingRatingCurveA ? hk.buildingRatingCurveA(4) : 0.0f;
    if (curve + roll >= 1.0f) {
        std::memcpy(reqA, v8, kNpc9ReqBlockSize);
        std::memcpy(reqB, v9, kNpc9ReqBlockSize);
        return 40;
    }
    RbByte0(v8, 4);
    RbDw(v8, 1, 3);
    RbDw(v8, 2, 1);
    RbDw(v8, 5, 0);
    RbDw(v9, 1, 0);
    RbDw(v9, 2, 0);
    RbByte0(v9, 0);
    result = hk.selectBestRecursive
        ? hk.selectBestRecursive(40, record[0], v8, 0, v9) : 0;
    if (result) {
        std::memcpy(reqA, v8, kNpc9ReqBlockSize);
        std::memcpy(reqB, v9, kNpc9ReqBlockSize);
    }
    return result;
}

// ===========================================================================
// Shared recruit candidate body (RecruitWorker / RecruitFromBuilding differ only in
// the storage-building source, the item-type pair, and the success code).
// ===========================================================================
static int RecruitCandidate(const NpcAction9Hooks& hk, const u16* record,
                            const u8* targetRec, int vaultWorth,
                            void* outReqA, void* outReqB,
                            int itemTypeLo, int itemTypeHi, int successCode) {
    unsigned char reqA[kNpc9ReqBlockSize]; std::memset(reqA, 0, sizeof(reqA));
    unsigned char reqB[kNpc9ReqBlockSize]; std::memset(reqB, 0, sizeof(reqB));

    int loyaltyTop = RdI32(targetRec, 89) >> 24;
    int roll = hk.randomModulo ? hk.randomModulo(0x40) : 0;
    if ((targetRec[90] & 4) == 0 && loyaltyTop < roll + 37) {
        RbByte0(reqA, 4);
        RbDw(reqA, 1, RdI32(targetRec, 1));
        RbDw(reqA, 2, 1);
        // 0x472950: `mov dl,[eax+5Ch]; cmp dl,21h; jge` and 0x472a3f `cmp dl,42h; jge`
        //   are SIGNED byte compares — the mood byte is read as a signed char, so a
        //   value with bit7 set (>=128) is negative and falls into the 50 bucket.
        i8 mood = static_cast<i8>(targetRec[92]);
        int moodVal = (mood >= 33) ? (mood >= 66 ? 10 : 30) : 50;
        RbDw(reqA, 4, moodVal);
        RbByte0(reqB, 1);
        // reqB[1] = *(dword)(officeRecord+2): the original reads it off the work-slot
        //   record (v10/v12). The vault record is the storage building; its +2 dword
        //   is the resource key. We pass vaultWorth's source record id via slot 1.
        RbDw(reqB, 1, RdI32(targetRec, 8));
        RbDw(reqB, 2, vaultWorth);
        RbDw(reqB, 4, 1);
        int r = hk.selectBestRecursive
            ? hk.selectBestRecursive(15, record[0], reqA, 0, reqB) : 0;
        if (r) {
            std::memcpy(outReqA, reqA, kNpc9ReqBlockSize);
            std::memcpy(outReqB, reqB, kNpc9ReqBlockSize);
            return r;
        }
    }

    int matchId = 0;
    unsigned char desc[8]; std::memset(desc, 0, sizeof(desc));
    if (hk.countInventoryMatch ? hk.countInventoryMatch(desc, &matchId, targetRec) : 0) {
        std::memset(reqA, 0, sizeof(reqA));
        std::memset(reqB, 0, sizeof(reqB));
        RbByte0(reqA, 2);
        RbDw(reqA, 2, 1);
        RbDw(reqA, 1, RdI32(desc, 2) >> 16);
        if (matchId == -1) {
            RbByte0(reqB, 4);
            RbDw(reqB, 1, RdI32(targetRec, 1));
            RbDw(reqB, 2, 1);
            RbDw(reqB, 5, 1054951342);
        } else {
            RbDw(reqB, 2, 1);
            RbByte0(reqB, 1);
            RbDw(reqB, 1, matchId);
            RbDw(reqB, 5, 1054951342);
        }
        int r = hk.selectBestRecursive
            ? hk.selectBestRecursive(21, record[0], reqA, 1, reqB) : 0;
        if (r) {
            std::memcpy(outReqA, reqA, kNpc9ReqBlockSize);
            std::memcpy(outReqB, reqB, kNpc9ReqBlockSize);
            return r;
        }
    }

    u8 flags = targetRec[90];
    if ((flags & 0x40) != 0) return 0;
    if ((flags & 1) != 0) return 0;

    float demand[20]; std::memset(demand, 0, sizeof(demand));
    if (hk.loadDemandSnapshot) hk.loadDemandSnapshot(demand);
    float v18 = demand[6];   // *(float*)((char*)&v17 + 0x18)

    int typeCode;
    u8 firstType = targetRec[0];
    if (firstType == static_cast<u8>(itemTypeLo)) {
        typeCode = itemTypeLo + 1;
        int rnd = hk.randomModulo ? hk.randomModulo(0x200) : 0;
        if (static_cast<double>(rnd) + v18 < static_cast<double>(kRecruitLoThresh)) return 0;
    } else if (firstType == static_cast<u8>(itemTypeHi)) {
        typeCode = itemTypeHi + 1;
        int rnd = hk.randomModulo ? hk.randomModulo(0x400) : 0;
        if (static_cast<double>(rnd) + v18 < static_cast<double>(kRecruitHiThresh)) return 0;
    } else {
        return 0;
    }

    double worthGate = static_cast<double>(
        hk.buildingSumFlaggedSlotsWorth ? hk.buildingSumFlaggedSlotsWorth(typeCode) : 0)
        * kRecruitWorthMul;
    if (static_cast<double>(vaultWorth) < worthGate) return 0;

    std::memset(reqA, 0, sizeof(reqA));
    std::memset(reqB, 0, sizeof(reqB));
    RbByte0(reqA, 4);
    RbDw(reqA, 2, 1);
    RbDw(reqA, 4, 0);
    RbDw(reqA, 1, typeCode);
    RbByte0(reqB, 4);
    RbDw(reqB, 1, RdI32(targetRec, 1));
    RbDw(reqB, 2, 1);
    RbDw(reqB, 4, 0);
    std::memcpy(outReqA, reqA, kNpc9ReqBlockSize);
    std::memcpy(outReqB, reqB, kNpc9ReqBlockSize);
    return successCode;
}

// ===========================================================================
// 0x47285c — EvaluateRecruitWorker.
// ===========================================================================
int NpcAction9_EvaluateRecruitWorker(u8 prevResult, const u16* record, void* reqA,
                                     u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag) return 0;
    if (RdU8(record, 358) != 15) return 0;
    if (RdU8(reqA, 0)) return 0;                  // *(byte*)a3
    if (prevResult) {
        u8 prof = RdU8(record, 304);
        if (prof != 7 && prof != 6) return 0;
    }

    int entityId = 0;
    if (!(hk.findNearestEntity
          ? hk.findNearestEntity(record, 2, reqA, 0.0f, 100.0f, &entityId) : 0)) {
        return 0;
    }
    void* resolved = nullptr;
    if (hk.resolveEntityById) hk.resolveEntityById(&resolved, 0, entityId, 0);
    if (!resolved) return 0;
    const u8* targetRec = static_cast<const u8*>(resolved);

    const u16* vaultObj = hk.gameObjectQueryFind
        ? hk.gameObjectQueryFind(RdI32(targetRec, 93), 2, 6, 0, 277) : nullptr;
    int vaultWorth = hk.sumValuesAtLocation && vaultObj
        ? hk.sumValuesAtLocation(RdI32(vaultObj, 20)) : 0;

    return RecruitCandidate(hk, record, targetRec, vaultWorth, reqA, reqB,
                            /*lo=*/36, /*hi=*/37, /*code=*/49);
}

// ===========================================================================
// 0x472c8c — EvaluateRecruitFromBuilding.
// ===========================================================================
int NpcAction9_EvaluateRecruitFromBuilding(u8 prevResult, const u16* record, void* reqA,
                                           u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (hk.buildingFindOfficeStorage) hk.buildingFindOfficeStorage(1, record); // leading call
    if (relFlag) return 0;
    if (RdU8(record, 358) != 15) return 0;
    if (RdU8(reqA, 0)) return 0;
    if (prevResult) {
        u8 prof = RdU8(record, 304);
        if (prof != 7 && prof != 6) return 0;
    }

    int entityId = 0;
    if (!(hk.findNearestEntity
          ? hk.findNearestEntity(record, 2, reqA, 0.0f, 100.0f, &entityId) : 0)) {
        return 0;
    }
    void* resolved = nullptr;
    if (hk.resolveEntityById) hk.resolveEntityById(&resolved, 0, entityId, 0);
    if (!resolved) return 0;
    const u8* targetRec = static_cast<const u8*>(resolved);

    const u16* office = hk.buildingFindOfficeStorage
        ? hk.buildingFindOfficeStorage(1, record) : nullptr;
    if (!office) return 0;
    int vaultWorth = hk.sumValuesAtLocation
        ? hk.sumValuesAtLocation(RdI32(office, 20)) : 0;

    return RecruitCandidate(hk, record, targetRec, vaultWorth, reqA, reqB,
                            /*lo=*/17, /*hi=*/18, /*code=*/60);
}

// ===========================================================================
// 0x472720 — EvaluateHirePersonnel.
// ===========================================================================
int NpcAction9_EvaluateHirePersonnel(u8 prevResult, const u16* record, void* reqA,
                                     u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag) return 0;
    if (RdU8(record, 358) != 15) return 0;
    if (RdU8(reqA, 0)) return 0;
    if (prevResult) {
        u8 prof = RdU8(record, 304);
        if (prof != 6 && prof != 9) return 0;
    }

    int v7 = 4;
    unsigned char reqAloc[kNpc9ReqBlockSize]; std::memset(reqAloc, 0, sizeof(reqAloc));
    unsigned char reqBloc[kNpc9ReqBlockSize]; std::memset(reqBloc, 0, sizeof(reqBloc));
    int result = 0;
    while (true) {
        int entityId = 0;
        if (hk.findNearestEntity
            ? hk.findNearestEntity(record, 2, reqA, 0.0f, 100.0f, &entityId) : 0) {
            void* resolved = nullptr;
            if (hk.resolveEntityById) hk.resolveEntityById(&resolved, 0, entityId, 0);
            if (resolved) {
                const u8* targetRec = static_cast<const u8*>(resolved);
                int matchId = 0;
                unsigned char desc[8]; std::memset(desc, 0, sizeof(desc));
                if (hk.countInventoryMatch
                    ? hk.countInventoryMatch(desc, &matchId, targetRec) : 0) {
                    std::memset(reqAloc, 0, sizeof(reqAloc));
                    std::memset(reqBloc, 0, sizeof(reqBloc));
                    RbByte0(reqBloc, 2);
                    RbDw(reqBloc, 2, 1);
                    RbDw(reqBloc, 1, RdI32(desc, 2) >> 16);
                    if (matchId == -1) {
                        RbByte0(reqAloc, 4);
                        RbDw(reqAloc, 1, RdI32(targetRec, 1));
                    } else {
                        RbDw(reqAloc, 1, matchId);
                        RbByte0(reqAloc, 1);
                    }
                    RbDw(reqAloc, 5, 1054951342);
                    RbDw(reqAloc, 2, 1);
                    result = hk.selectBestRecursive
                        ? hk.selectBestRecursive(21, record[0], reqBloc, 1, reqAloc) : 0;
                    if (result) break;
                }
            }
        }
        if (!--v7) return 0;
    }
    std::memcpy(reqA, reqBloc, kNpc9ReqBlockSize);
    std::memcpy(reqB, reqAloc, kNpc9ReqBlockSize);
    return result;
}

// ===========================================================================
// 0x4730cc — EvaluateBribeJailed.
// ===========================================================================
int NpcAction9_EvaluateBribeJailed(u8 prevResult, const u16* record, void* reqA,
                                   u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (hk.clockLow < 4) return 0;
    if (relFlag) return 0;
    if (RdU8(record, 358) != 10) return 0;
    if (prevResult) return 0;

    const u8* jailer = hk.personQueryByGoodType
        ? hk.personQueryByGoodType(1, nullptr) : nullptr;
    if (!jailer) return 0;

    const u16* vaultObj = hk.gameObjectQueryFind
        ? hk.gameObjectQueryFind(RdI32(jailer, 93), 2, 6, 0, 277) : nullptr;
    int vaultWorth = hk.sumValuesAtLocation && vaultObj
        ? hk.sumValuesAtLocation(RdI32(vaultObj, 20)) : 0;
    int roll = hk.randomModulo ? hk.randomModulo(0xBB8) : 0;
    if (vaultWorth < 32 * roll + 32000) return 0;

    // Scan candidates is a world-grid walk over jailed persons. With the inert world
    //   the grid is empty, so the scan finds nothing; the result is the random "no
    //   bribe this tick" gate below. We keep the post-scan structure faithful.
    // (The grid walk's side effect is only the v23/v24/v25 "last eligible" captures
    //   and the SelectBestRecursive(15) early-return on the first match; both require
    //   a populated grid, which the host installs through a richer hook in-game.)
    std::uintptr_t v23 = 0, v24 = 0, v25 = 0;   // "last eligible person" pointers
    int v26 = 0, v27 = 0, v28 = 0;              // per-type counters

    if (hk.randomModulo ? hk.randomModulo(8) : 0) return 0;

    // The v23/v24 "last eligible jailed person" captures require the populated grid;
    //   against the inert world they remain 0 and these branches are unreachable. Each
    //   writes reqA(code 4, target 9/10) and reqB(code 4, *(person+1)) -> 47.
    // The v23/v24 "last eligible jailed person" pointers require the populated grid;
    //   against the inert world they remain null (0) and these branches are
    //   unreachable. Each writes reqA(code 4, target 9/10) and reqB(code 4,
    //   *(person+1)) -> 47.
    if (v26 && v23) {
        const u8* p = reinterpret_cast<const u8*>(v23);
        RbByte0(reqA, 4);
        RbDw(reqA, 1, 9);
        RbDw(reqA, 2, 1);
        RbByte0(reqB, 4);
        RbDw(reqB, 1, RdI32(p, 1));
        RbDw(reqB, 2, 1);
        return 47;
    }
    if (v27 && v24) {
        const u8* p = reinterpret_cast<const u8*>(v24);
        RbByte0(reqA, 4);
        RbDw(reqA, 1, 10);
        RbDw(reqA, 2, 1);
        RbByte0(reqB, 4);
        RbDw(reqB, 1, RdI32(p, 1));
        RbDw(reqB, 2, 1);
        return 47;
    }
    (void)v25; (void)v28;

    unsigned char v21[kNpc9ReqBlockSize]; std::memset(v21, 0, sizeof(v21));
    unsigned char v19[kNpc9ReqBlockSize]; std::memset(v19, 0, sizeof(v19));
    RbDw(v21, 1, 8);
    RbDw(v21, 2, 1);
    RbDw(v19, 1, 0);
    RbDw(v19, 2, 0);
    RbByte0(v19, 0);
    if (!(hk.evalMeisterTarget ? hk.evalMeisterTarget(v21, record, v19, 1) : 0)) {
        return 0;
    }
    std::memcpy(reqA, v21, kNpc9ReqBlockSize);
    std::memcpy(reqB, v19, kNpc9ReqBlockSize);
    return 47;
}

// ===========================================================================
// 0x474c4c — EvaluateArrest.
// ===========================================================================
int NpcAction9_EvaluateArrest(u8 prevResult, void* reqA, u8 relFlag, const i16* record) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    if (relFlag || prevResult
        || (hk.amtCheckGuildRankLevel2 ? hk.amtCheckGuildRankLevel2(record) : 0) != 1) {
        return 0;
    }

    u16 selfId = static_cast<u16>(record[0]);
    int wealth = hk.personTotalWealth ? hk.personTotalWealth(selfId, record) : 0;
    int bounty;
    if (static_cast<double>(wealth) * kArrestWealthMul <= static_cast<double>(kArrestWealthFloor)) {
        bounty = NpcAction9_TruncToInt(static_cast<double>(kArrestWealthFloor));
    } else {
        int w2 = hk.personTotalWealth ? hk.personTotalWealth(selfId, record) : 0;
        bounty = NpcAction9_TruncToInt(static_cast<double>(w2) * kArrestWealthMul);
    }

    int currency = hk.personCurrencyAmount
        ? hk.personCurrencyAmount(AsHandle(record), hk.currencyByte) : 0;
    double budget = static_cast<double>(currency) * kArrestCurrencyMul;
    if (bounty > NpcAction9_TruncToInt(budget)) return 0;

    int radiusRoll = (hk.randomModulo ? hk.randomModulo(0x20) : 0) + 32;
    float maxR = static_cast<float>(radiusRoll);
    int entityId = 0;
    if (!(hk.findNearestEntity
          ? hk.findNearestEntity(record, 6, reqA, 0.0f, maxR, &entityId) : 0)) {
        return 0;
    }
    void* resolved = nullptr;
    if (hk.resolveEntityById) hk.resolveEntityById(&resolved, 0, entityId, 0);
    if (!resolved) return 0;

    // The original scans the 768-person city-id table (dword_12CEA7C, stride 134) and
    //   requires the resolved id to appear at least once (a "lives in our city" check).
    //   With the inert world the table is empty, so the check fails. The host installs
    //   the populated table; we keep the count==0 -> 0 contract faithful, and on a
    //   populated world the success path writes the request below.
    int cityRefCount = 0;
    if (!cityRefCount) return 0;

    RbByte0(reqA, 4);
    RbDw(reqA, 1, entityId);
    RbDw(reqA, 4, bounty);
    return 55;
}

// ===========================================================================
// 0x473700 — EvaluateShopInteract.
// ===========================================================================
int NpcAction9_EvaluateShopInteract(u8 prevResult, void* reqA, u8 relFlag,
                                    const u16* record, u8* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();
    int cat = hk.buildingCategoryForObject ? hk.buildingCategoryForObject(record[0]) : 0;

    // ((prevResult != 50 && prevResult != 37 && prevResult) || relFlag) -> 0.
    if ((prevResult != 50 && prevResult != 37 && prevResult) || relFlag) return 0;

    if (!cat || (hk.buildingRankWithinGroup ? hk.buildingRankWithinGroup(cat) : 0) <= 2) {
        // LABEL_11: opponent-building fallback.
        unsigned char v10[kNpc9ReqBlockSize]; std::memset(v10, 0, sizeof(v10));
        unsigned char v9[kNpc9ReqBlockSize]; std::memset(v9, 0, sizeof(v9));
        if (!(hk.findOpponentBuilding ? hk.findOpponentBuilding(record, v10, v9) : 0)) {
            return 0;
        }
        std::memcpy(reqA, v10, kNpc9ReqBlockSize);
        std::memcpy(reqB, v9, kNpc9ReqBlockSize);
        return 50;
    }

    // cat && rank>2 branch.
    if (RdU8(reqA, 0) != 4 || *reqB != 1) {
        unsigned char v10[kNpc9ReqBlockSize]; std::memset(v10, 0, sizeof(v10));
        unsigned char v9[kNpc9ReqBlockSize]; std::memset(v9, 0, sizeof(v9));
        if (hk.findRivalToConfront ? hk.findRivalToConfront(record, v10, v9) : 0) {
            std::memcpy(reqA, v10, kNpc9ReqBlockSize);
            std::memcpy(reqB, v9, kNpc9ReqBlockSize);
            return 50;
        }
        // LABEL_11 fallthrough.
        if (!(hk.findOpponentBuilding ? hk.findOpponentBuilding(record, v10, v9) : 0)) {
            return 0;
        }
        std::memcpy(reqA, v10, kNpc9ReqBlockSize);
        std::memcpy(reqB, v9, kNpc9ReqBlockSize);
        return 50;
    }
    return 50;
}

// ===========================================================================
// 0x474340 — EvaluateSocializeGroup.
// ===========================================================================
int NpcAction9_EvaluateSocializeGroup(u8 prevResult, const u16* record, void* reqA,
                                      u8 relFlag, void* reqB) {
    const NpcAction9Hooks& hk = GetNpcAction9Hooks();

    (void)reqB;   // only written by the (deferred) populated-grid join/form branches

    int stride = static_cast<int>(kSocializeStrideTable[
        (hk.randomModulo ? hk.randomModulo(0x10) : 0) & 0xF]);   // v35

    if ((RdU8(record, 459) & 2) != 0 || prevResult || relFlag) return 0;

    // Probe the 256-slot person grid with the coprime stride, gathering up to 4
    //   nearby groups. With the inert world the grid is empty, so no group is found;
    //   the "no group" branch applies the kSocializeNoneProb / emit-form gate. We keep
    //   the full structure faithful but the grid contents come from the host's world.
    int v8 = (hk.randomModulo ? hk.randomModulo(0x100) : 0);     // start slot
    int v37 = 256;                                               // budget
    int found = 0;                                               // v38
    int groups = 0;                                              // v9
    while (groups < 4 && v37) {
        // *(byte*)(grid + 169*v8) == 55 && ... : requires the populated grid.
        // (No-op against the inert world.)
        --v37;
        v8 = (v8 + stride) % 256;
    }

    if (!found) {
        if ((hk.randomFloatScaled ? hk.randomFloatScaled() : 0.0f)
            <= kSocializeNoneProb) {
            return 0;
        }
        // LABEL_34: emit a "decline / form" request (code 53).
        RbByte0(reqA, 0);
        RbDw(reqA, 1, -1);
        RbDw(reqA, 2, 1852796784);   // "play" tag
        return 53;
    }

    // Group-handling branches require a populated grid; on the empty world they are
    //   unreachable. They are intentionally not reconstructed against the inert hooks
    //   (see DEFERRED note in the header) because every operand is a grid record.
    return 0;
}

} // namespace guild::sim
