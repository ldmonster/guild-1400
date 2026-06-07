#include "world/guild_election.h"

#include <cstring>

#include "crt/rand.h"
#include "world/office.h"   // g_officeHolders, OfficeHolder

// Faithful 1:1 port of the deterministic decision rules of the Amt guild
// governance passes. The Person walk, the AI scoring (VIBE_AiPlayer_*), the
// building spawn (Universe/render/io) and the network commit are owned elsewhere
// and routed through hooks. RNG via guild::crt::RandNext (VIBE_Math_RandomModulo).

namespace guild::world {

// ===========================================================================
// 1. ElectGuildMaster (full) — 0x481228.
// ===========================================================================
namespace {
GuildInstallHook g_gmInstall = nullptr;
GuildNotifyHook  g_gmNotify  = nullptr;
void*            g_gmCtx     = nullptr;
} // namespace

void GuildSetElectionHooks(GuildInstallHook install, GuildNotifyHook notify,
                           void* ctx) {
    g_gmInstall = install;
    g_gmNotify  = notify;
    g_gmCtx     = ctx;
}

// Shared election decision used by ElectGuildMasterFull and CalcZunftElection.
// `inRange(p)` selects the candidate predicate (employer office-type range vs.
// held-rank range); the member count is every employed non-flagged person.
namespace {
template <typename Pred>
GuildElectionResult RunElection(const GuildPerson* pool, int count,
                                u8 masterOfficeType, Pred candidatePred) {
    GuildElectionResult r;
    if (!pool || count <= 0)
        return r;

    // Collect candidates (dedup by id, cap 16 == v16[16]) + count members.
    i32 candIds[16];
    i32 candWealth[16];
    int collected = 0;
    int members = 0;
    for (int i = 0; i < count && collected < 16; ++i) {
        const GuildPerson& p = pool[i];
        if (p.employer == 0xFFFFu || p.flagged)
            continue;
        ++members; // ++v2 for every employed non-flagged person
        if (!candidatePred(p))
            continue;
        bool dup = false;
        for (int k = 0; k < collected; ++k)
            if (candIds[k] == p.personId) { dup = true; break; }
        if (dup)
            continue;
        candIds[collected]    = p.personId;
        candWealth[collected] = p.totalWealth;
        ++collected;
    }
    r.members    = members;
    r.candidates = collected;

    // Quorum: v2 >= 3 && v1 (collected) >= 1.
    if (!(members >= 3 && collected >= 1))
        return r;

    // Incumbent lookup: scan the holder table backwards for the guild-master seat
    // (byte_B59850[idx] == masterOfficeType). result=864 step -24 == idx 36..0.
    int slot = -1;
    i32 incumbent = -1;
    for (int idx = kAemterCount - 1; idx >= 0; --idx) {
        if (g_officeHolders[idx].type == masterOfficeType) {
            // v17 = 24-byte entry; v17[0] = +0 dword (slot key), v17[1] = +4 (.city).
            slot      = static_cast<int>(g_officeHolders[idx].holder); // +0 byte
            incumbent = g_officeHolders[idx].city;                     // +4 dword
            break; // --v7 == 0 after the first hit
        }
    }
    r.officeSlot   = slot;
    r.incumbentId  = incumbent;

    // Winner: strictly-greater total wealth (first of equal wealth wins). The
    // original seeds the best at ComputeTotalWealth(...) of the incumbent if any,
    // with v9=0; here v9 starts null so the first candidate becomes the leader.
    int bestIdx = 0;
    i32 bestWealth = candWealth[0];
    for (int k = 1; k < collected; ++k) {
        if (candWealth[k] > bestWealth) {
            bestWealth = candWealth[k];
            bestIdx    = k;
        }
    }
    r.winnerId = candIds[bestIdx];

    // Install gate: winner exists && winner != incumbent (v9 && v9 != v8).
    r.install = (r.winnerId != incumbent);
    if (r.install) {
        if (g_gmInstall)
            g_gmInstall(slot, r.winnerId, g_gmCtx);
        // Notify old (when present) then new, both with message 0x22.
        if (g_gmNotify) {
            if (incumbent != -1)
                g_gmNotify(true, incumbent, 0x22, g_gmCtx);
            g_gmNotify(false, r.winnerId, 0x22, g_gmCtx);
        }
    }
    return r;
}
} // namespace

GuildElectionResult ElectGuildMasterFull(const GuildPerson* pool, int count) {
    // Candidate: employer office-type byte (+356) in [13..18].
    return RunElection(pool, count, kGuildMasterOfficeType,
                       [](const GuildPerson& p) {
                           return p.empOfficeType >= 13 && p.empOfficeType <= 18;
                       });
}

// ===========================================================================
// 2. CalcZuenfte — 0x4813d0.
// ===========================================================================
ZunftCategories ZunftCategoriesForRank(u8 rank) {
    ZunftCategories z{};
    z.valid = true;
    switch (rank) {
    case 0x1E: z.memberQuery=14; z.officeLow=39; z.officeHigh=34; z.reqQuery=56; z.masterQuery=23; break;
    case 0x1F: z.memberQuery=18; z.officeLow=51; z.officeHigh=46; z.reqQuery=59; z.masterQuery=24; break;
    case 0x20: z.memberQuery=20; z.officeLow=63; z.officeHigh=58; z.reqQuery=62; z.masterQuery=25; break;
    case 0x21: z.memberQuery=21; z.officeLow=69; z.officeHigh=64; z.reqQuery=65; z.masterQuery=26; break;
    default:   z.valid = false; break;
    }
    return z;
}

GuildElectionResult CalcZunftElection(u8 rank, const GuildPerson* pool, int count) {
    ZunftCategories z = ZunftCategoriesForRank(rank);
    if (!z.valid)
        return GuildElectionResult{};
    // In the original the gate is: heldRank(+353>>24) >= (v78>>24) && <= (v76>>24).
    // The recovered fields put the office type (e.g. 34) in officeHigh and the low
    // bound (e.g. 39) in officeLow — i.e. the band is [min,max] of the two.
    u8 lo = z.officeHigh < z.officeLow ? z.officeHigh : z.officeLow;
    u8 hi = z.officeHigh < z.officeLow ? z.officeLow : z.officeHigh;
    return RunElection(pool, count, rank,
                       [lo, hi](const GuildPerson& p) {
                           return p.rankHighByte >= lo && p.rankHighByte <= hi;
                       });
}

// ===========================================================================
// 3. ComputeGuildAssignment — 0x47ff5c.
// ===========================================================================
// Relation delta for the winning-side assignment (v29/v30 in the original):
//   approach 0 -> -20, approach 1 -> +20 (v30=20), else (2) -> +4.
i32 GuildAssignDeltaWin(int approach) {
    if (approach == 0) return -20;
    if (approach == 1) return 20;
    return 4;
}

GuildAssignDeltaLose GuildAssignDeltaLoseFor(int approach) {
    GuildAssignDeltaLose d;
    if (approach == 0)
        d.emitNeg10 = true;          // QueueRequestCoord27(.., -10) first
    d.delta = (approach == 1) ? 10 : -4;
    return d;
}

int GuildCountByState(const OfficeHolder* holders, int count, u8 state) {
    if (!holders || count <= 0)
        return 0;
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (holders[i].state == state)
            ++n;
    return n;
}

// gilde.exe 0x47ff5c — successor tie-break (the v41..v48 block):
//   scan tallies; track max (strictly-greater replaces, equal sets the "tie"
//   flag); if a tie exists, walk forward from a random start (RandomModulo(n))
//   until a tally equal to the max is found and pick that index; else the unique
//   max index.
int GuildSuccessorPick(const i32* voteTallies, int n) {
    if (!voteTallies || n <= 0)
        return -1;

    bool tie = true;       // v41 (the original seeds it 1; cleared on strict max)
    i32  best = -1;        // v42
    int  bestIdx = -1;     // v44
    for (int i = 0; i < n; ++i) {
        i32 v = voteTallies[i];
        if (best >= v) {
            if (best == v)
                tie = true;
        } else {
            best = v;
            bestIdx = i;
            tie = false;
        }
    }

    if (tie) {
        // Walk forward from a random start until a tally equal to best is hit.
        int rem = n;
        int j = static_cast<u16>(guild::crt::RandNext() % (n ? n : 1)); // RandomModulo(n)
        while (voteTallies[j] != best) {
            j = (j + 1) % n;
            if (--rem == 0)
                return bestIdx; // LABEL_99: fall back to the tracked max
        }
        return j;
    }
    return bestIdx;
}

// ===========================================================================
// 4. RunProductionPass — 0x57d448.
// ===========================================================================
namespace {
ProductionStateHook     g_prodState = nullptr;
ProductionHireHook      g_prodHire  = nullptr;
ProductionGoodsDistHook g_prodGoods = nullptr;
void*                   g_prodCtx   = nullptr;
} // namespace

void ProductionSetHooks(ProductionStateHook state, ProductionHireHook hire,
                        ProductionGoodsDistHook goods, void* ctx) {
    g_prodState = state;
    g_prodHire  = hire;
    g_prodGoods = goods;
    g_prodCtx   = ctx;
}

ProductionResult RunProductionPass(const ProductionPerson* persons, int personCount,
                                   const ProductionBuilding* buildings,
                                   int buildingCount) {
    ProductionResult r;

    // Phase 1: every profession-18 person -> "set state 8" delta-field commit.
    if (persons) {
        for (int i = 0; i < personCount; ++i) {
            if (persons[i].profClass == 18) {
                if (g_prodState)
                    g_prodState(persons[i].personId, 8, g_prodCtx);
                ++r.stateFlips;
            }
        }
    }

    // Phase 2: active buildings tick production; on the daily-output boundary with
    // a free worker slot, emit a hire string command.
    if (buildings) {
        for (int i = 0; i < buildingCount; ++i) {
            const ProductionBuilding& b = buildings[i];
            if (b.profClass < 10 && b.productionFlag) {
                ++r.ticks; // VIBE_Building_UpdateProductionState
                if (b.atBoundary && b.hasWorkerSlot) {
                    if (g_prodHire)
                        g_prodHire(b.objectId, b.femaleWorker, g_prodCtx);
                    ++r.hires;
                }
            }
        }
    }

    // Tail: the goods-distribution pass.
    if (g_prodGoods)
        g_prodGoods(g_prodCtx);
    r.goodsRan = true;
    return r;
}

// ===========================================================================
// 5. ProcessAllOfficeWages — 0x57b6bc.
// ===========================================================================
int ProcessAllOfficeWages(OfficeWagePayHook payHook, void* ctx) {
    for (int seat = 0; seat < kOfficeWageSeats; ++seat) {
        if (payHook)
            payHook(seat, ctx); // VIBE_Amt_ComputeOfficeWages(seat, 1, ..)
    }
    return kOfficeWageSeats;
}

// ===========================================================================
// 6. UpdateOffices — 0x481978.
// ===========================================================================
OfficeRankBand OfficeRankBandFor(u8 officeType) {
    OfficeRankBand b{};
    b.valid = true;
    switch (officeType) {
    case 0x1C: b.low=1;  b.high=6;  break;
    case 0x1D: b.low=40; b.high=45; break;
    case 0x1E: b.low=34; b.high=39; break;
    case 0x1F: b.low=46; b.high=51; break;
    case 0x20: b.low=58; b.high=63; break;
    case 0x21: b.low=64; b.high=69; break;
    case 0x22: b.low=13; b.high=18; break;
    default:   b.valid = false; break;
    }
    return b;
}

bool OfficeShouldRemove(u8 officeType, const OfficeValidatePerson& p) {
    OfficeRankBand b = OfficeRankBandFor(officeType);
    // The original's removal condition (negated "keep"):
    //   !person || !(record+8) || officeType != (record+361)
    //   || heldRankHigh < low || heldRankHigh > high
    if (!p.exists)              return true;
    if (!p.hasBuilding)         return true;
    if (officeType != p.heldOfficeType) return true;
    if (b.valid) {
        if (p.heldRankHigh < b.low)  return true;
        if (p.heldRankHigh > b.high) return true;
    }
    return false;
}

// ===========================================================================
// 7. SaveAemter / LoadAemter — 0x483198 / 0x4832d0.
// ===========================================================================
namespace {
void PutU32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
}
u32 GetU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8)
         | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
i32 GetI32(const u8* p) { return static_cast<i32>(GetU32(p)); }
} // namespace

size_t SaveAemter(u8* out, size_t cap) {
    if (!out || cap < static_cast<size_t>(kAemterStreamBytes))
        return 0;
    // Leading count (v5[0] == 37).
    PutU32(out, static_cast<u32>(kAemterCount));
    u8* w = out + 4;
    for (int i = 0; i < kAemterCount; ++i) {
        const OfficeHolder& h = g_officeHolders[i];
        // byte +0, dword +4, byte +8, dword +12, byte +16, dword +20.
        *w++ = h.holder;
        PutU32(w, static_cast<u32>(h.city)); w += 4;
        *w++ = h.type;
        PutU32(w, static_cast<u32>(h.rank)); w += 4;
        *w++ = h.state;
        // The original writes the dword at +20 (the secondary holder id).
        PutU32(w, static_cast<u32>(h.secondary)); w += 4;
    }
    return kAemterStreamBytes;
}

int LoadAemter(const u8* in, size_t len) {
    if (!in || len < static_cast<size_t>(kAemterStreamBytes))
        return 0;
    if (GetU32(in) != static_cast<u32>(kAemterCount)) // count read must be 37
        return 0;
    const u8* rd = in + 4;
    for (int i = 0; i < kAemterCount; ++i) {
        OfficeHolder& h = g_officeHolders[i];
        h.holder = *rd++;
        h.city   = GetI32(rd); rd += 4;
        h.type   = *rd++;
        h.rank   = GetI32(rd); rd += 4;
        h.state  = *rd++;
        h.secondary = GetI32(rd); rd += 4; // +20 secondary holder id
    }
    return 1;
}

} // namespace guild::world
