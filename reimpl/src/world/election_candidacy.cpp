#include "world/election_candidacy.h"

#include "world/office.h"      // g_officeHolders, OfficeHolder
#include "world/law_types.h"   // kAemterCount-equivalent (kOfficeDefCount)

// Faithful 1:1 port of the two-seat guild-master canvass (VIBE_Amt_*). The Person
// walk (QueryBegin/IterNext), the ComputeTotalWealth scoring, the office-table
// install and the entity-message transport are owned by sim/io/command and routed
// through pools + hooks; recovered here are the deterministic decision rules.

namespace guild::world {

// ===========================================================================
// 1. CollectGuildCandidates — 0x480e4c.
// ===========================================================================
namespace {
CanvassInstallHook g_canvassInstall = nullptr;
CanvassNotifyHook  g_canvassNotify  = nullptr;
void*              g_canvassCtx     = nullptr;
bool               g_haveIncRank    = false;
guild::u8          g_incRankHigh    = 0;

// Collect one sweep into the shared candidate table (dedup, cap 16). Mirrors the
// two near-identical do/while loops: for every employed (employer != 0xFFFF),
// non-flagged person, ++members; if the employer category is in [lo..hi] and the
// person id is not already collected, append (until 16 are held).
void CanvassSweep(const CanvassPerson* pool, int count, guild::u8 lo, guild::u8 hi,
                  i32* ids, i32* wealth, u8* winnerCat, int& collected,
                  int& members) {
    if (!pool || count <= 0)
        return;
    for (int i = 0; i < count && collected < kCanvassMaxCands; ++i) {
        const CanvassPerson& p = pool[i];
        if (p.employer == 0xFFFFu || p.flagged)
            continue;
        ++members; // ++v2 for every employed non-flagged person
        if (!(p.empCategory >= lo && p.empCategory <= hi))
            continue;
        // Dedup against the v33 table (the v24/v25 and v26/v27 scans).
        bool dup = false;
        for (int k = 0; k < collected; ++k) {
            if (ids[k] == p.personId) { dup = true; break; }
        }
        if (dup)
            continue;
        ids[collected]       = p.personId;
        wealth[collected]    = p.totalWealth;
        winnerCat[collected] = p.winnerCategory;
        ++collected;
    }
}
} // namespace

void CanvassSetHooks(CanvassInstallHook install, CanvassNotifyHook notify,
                     void* ctx) {
    g_canvassInstall = install;
    g_canvassNotify  = notify;
    g_canvassCtx     = ctx;
}

u8 CanvassSetIncumbentRank(u8 rankHigh) {
    u8 prev       = g_incRankHigh;
    g_incRankHigh = rankHigh;
    g_haveIncRank = true;
    return prev;
}

CanvassResult CollectGuildCandidates(const CanvassPerson* band1, int n1,
                                     const CanvassPerson* band2, int n2) {
    CanvassResult r;

    // The v33[16] candidate table + the two parallel score/category tables.
    i32 ids[kCanvassMaxCands];
    i32 wealth[kCanvassMaxCands];
    u8  winnerCat[kCanvassMaxCands];
    int collected = 0;
    int members   = 0;

    // Pass 1: employer category [1..6]. Pass 2: [40..45].
    CanvassSweep(band1, n1, kCanvassBand1Min, kCanvassBand1Max,
                 ids, wealth, winnerCat, collected, members);
    CanvassSweep(band2, n2, kCanvassBand2Min, kCanvassBand2Max,
                 ids, wealth, winnerCat, collected, members);

    r.members    = members;
    r.candidates = collected;

    // Quorum: v2 (members) >= 3 && v1 (candidates) >= 1.
    if (members < kCanvassQuorum || collected == 0)
        return r;
    r.quorumMet = true;

    // Dual-seat incumbent lookup. The original scans byte_B59850 (holder +8) from
    // entry 36 down (result=864, step -24), copying the type-29 entry into v34 and
    // the type-28 entry into v36; v13 counts down from 2 (both seats found). v34/
    // v36 are the +0 holder keys; v35/v37 are the +20 secondaries (the holders).
    int seatMaster = -1; // v34
    int seatDeputy = -1; // v36
    i32 secMaster  = -1; // v35
    i32 secDeputy  = -1; // v37
    {
        int need = 2;
        // result=864 -> entry 36 (864/24), step -24 down to entry 0.
        for (int idx = kOfficeDefCount - 1; idx >= 0 && need; --idx) {
            const OfficeHolder& h = g_officeHolders[idx];
            if (h.type == kSeatTypeMaster) {       // byte_B59850 == 29
                seatMaster = static_cast<int>(h.holder); // v34 = +0
                secMaster  = h.secondary;                // v35 = +20
                --need;
            } else if (h.type == kSeatTypeDeputy) { // byte_B59850 == 28
                seatDeputy = static_cast<int>(h.holder); // v36 = +0
                secDeputy  = h.secondary;                // v37 = +20
                --need;
            }
        }
    }
    r.seatMaster = seatMaster;
    r.seatDeputy = seatDeputy;

    // Incumbent = v37 if != -1 else v35 (the deputy seat takes precedence).
    i32 incumbent = -1;
    if (secDeputy != -1)
        incumbent = secDeputy;
    else if (secMaster != -1)
        incumbent = secMaster;
    r.incumbentId = incumbent;
    bool haveIncumbent = (incumbent != -1);
    u8 incRankHigh = g_haveIncRank ? g_incRankHigh : 0;
    r.incumbentRankHigh = incRankHigh;

    // Winner: wealthiest candidate (v18). The original seeds v18=0 (no leader) and
    // the running best wealth (v21) from the incumbent's ComputeTotalWealth, then
    // replaces v18 only when a candidate's wealth is STRICTLY greater. So the first
    // candidate strictly above the incumbent leads; equal-wealth candidates do not
    // displace the leader (first-found-max). When the seat is vacant the seed is
    // effectively "below everything" so the first candidate leads.
    int bestIdx   = -1;
    i32 bestWealth = -2147483647 - 1; // v21 seed (incumbent wealth; vacant -> min)
    for (int k = 0; k < collected; ++k) {
        if (wealth[k] > bestWealth) {
            bestWealth = wealth[k];
            bestIdx    = k;
        }
    }
    if (bestIdx < 0)
        return r; // no candidate beat the incumbent seed -> no install (v18==0)
    r.winnerIndex = bestIdx;
    r.winnerId    = ids[bestIdx];
    u8 winCat     = winnerCat[bestIdx];
    u8 winRankHigh = 0; // (winner record+353)>>24 -> caller-derived; 0 if absent

    // Install gate: v18 && v18 != v16 (winner exists and != incumbent).
    if (r.winnerId == incumbent)
        return r;
    r.install = true;

    // The seat split. NOT in [1..6] -> master seat (v34/29); in [1..6] -> deputy
    // seat (v36/28). (v31/v29/v22 == winner's +356 category byte.)
    auto isDeputyCat = [](u8 c) { return c >= 1 && c <= 6; };

    if (haveIncumbent) {
        // Rank-distance gate: |incRankHigh - winRankHigh| < 6.
        int d = static_cast<int>(incRankHigh) - static_cast<int>(winRankHigh);
        if (d < 0) d = -d;
        if (d < kRankDistanceGate) {
            // Same-seat path. Notify person(incumbent, +361) then office(winner).
            r.choice         = CanvassSeatChoice::kSameSeat;
            r.notifyMessage  = 0; // the original passes v16+361 (held office type)
            if (g_canvassNotify) {
                g_canvassNotify(false, incumbent, r.notifyMessage, g_canvassCtx);
                g_canvassNotify(true, r.winnerId, r.notifyMessage, g_canvassCtx);
            }
            r.installSeat = isDeputyCat(winCat) ? seatDeputy : seatMaster;
            if (g_canvassInstall)
                g_canvassInstall(r.installSeat, r.winnerId, g_canvassCtx);
        } else {
            // Far-rank swap path. Clear the OTHER seat, install into the winner's.
            if (isDeputyCat(winCat)) {
                // set deputy(v36, v18), clear master(v34, 0), msg 28.
                r.choice        = CanvassSeatChoice::kSwapDeputy;
                r.installSeat   = seatDeputy;
                r.clearedSeat   = seatMaster;
                r.notifyMessage = kSeatTypeDeputy; // 28
                if (g_canvassInstall) {
                    g_canvassInstall(seatDeputy, r.winnerId, g_canvassCtx);
                    g_canvassInstall(seatMaster, 0, g_canvassCtx);
                }
            } else {
                // clear deputy(v36, 0), set master(v34, v18), msg 29.
                r.choice        = CanvassSeatChoice::kSwapMaster;
                r.installSeat   = seatMaster;
                r.clearedSeat   = seatDeputy;
                r.notifyMessage = kSeatTypeMaster; // 29
                if (g_canvassInstall) {
                    g_canvassInstall(seatDeputy, 0, g_canvassCtx);
                    g_canvassInstall(seatMaster, r.winnerId, g_canvassCtx);
                }
            }
            if (g_canvassNotify) {
                g_canvassNotify(false, incumbent, 0, g_canvassCtx); // person(v16,+361)
                g_canvassNotify(true, r.winnerId, r.notifyMessage, g_canvassCtx);
            }
        }
    } else {
        // No incumbent. Install into the seat by category, notify office only.
        if (isDeputyCat(winCat)) {
            r.choice        = CanvassSeatChoice::kDeputySeatOnly;
            r.installSeat   = seatDeputy;
            r.notifyMessage = kSeatTypeDeputy; // v23 = 28
        } else {
            r.choice        = CanvassSeatChoice::kMasterSeatOnly;
            r.installSeat   = seatMaster;
            r.notifyMessage = kSeatTypeMaster; // v23 = 29
        }
        if (g_canvassInstall)
            g_canvassInstall(r.installSeat, r.winnerId, g_canvassCtx);
        if (g_canvassNotify)
            g_canvassNotify(true, r.winnerId, r.notifyMessage, g_canvassCtx);
    }
    return r;
}

// ===========================================================================
// 2. IsOfficeBuildingValid — 0x481b38.
// ===========================================================================
namespace {
ZuenfteFanHook g_zuenfteFan = nullptr;
void*          g_zuenfteCtx = nullptr;
} // namespace

void OfficeBuildingSetZuenfteHook(ZuenfteFanHook hook, void* ctx) {
    g_zuenfteFan = hook;
    g_zuenfteCtx = ctx;
}

int OfficeBuildingQueryCategory(u8 role) {
    switch (role) {
        case 1: return 24; // v3 = 24
        case 2: return 25; // v3 = 25
        case 3: return 26; // v3 = 26
        case 5: return 23; // v3 = 23
        default: return -1;
    }
}

int IsOfficeBuildingValid(const OfficeBuildingValidInputs& in, int arg) {
    if (in.handlerMatch) {
        // The handler-match branch: fan CalcZuenfte over ranks 0x1E..0x21.
        if (g_zuenfteFan) {
            g_zuenfteFan(0x1E, arg, g_zuenfteCtx);
            g_zuenfteFan(0x1F, arg, g_zuenfteCtx);
            g_zuenfteFan(0x20, arg, g_zuenfteCtx);
            g_zuenfteFan(0x21, arg, g_zuenfteCtx);
        }
        return 0;
    }
    // The else: a single member query for the mapped category.
    int cat = OfficeBuildingQueryCategory(in.role);
    if (cat < 0)
        return 0; // default: result = 0
    // QueryBegin(...) && (member+90 & 1) == 0 -> result 1.
    return in.memberPresent ? 1 : 0;
}

// ===========================================================================
// 3. NotifyOfficeMessage / NotifyPersonMessage — 0x480d08 / 0x480da8.
// ===========================================================================
namespace {
// Shared gate: profession in {5,6,7} -> broadcast, offset 560 if +9 set else 525.
bool NotifyGate(const NotifyTarget& t, u8 message, int& outArg) {
    u8 prof = t.profession;
    if (prof != 6 && prof != 7 && prof != 5)
        return false;
    int base = t.secondFlag ? kNotifyOffsetSet : kNotifyOffsetClear;
    outArg = static_cast<int>(message) + base;
    return true;
}
} // namespace

bool NotifyOfficeComputeArg(const NotifyTarget& t, u8 message, int& outArg) {
    // The office variant has no null guard (the original dereferences result).
    return NotifyGate(t, message, outArg);
}

bool NotifyPersonComputeArg(const NotifyTarget& t, u8 message, int& outArg) {
    if (!t.valid) // the person variant guards `if ( result )` first.
        return false;
    return NotifyGate(t, message, outArg);
}

int NotifyBroadcast(const NotifySlot* slots, int count, int subject, int arg,
                    NotifySendHook send, void* ctx) {
    if (!slots || count <= 0 || !send)
        return 0;
    int sent = 0;
    // The (v4 += 134; v4 != 102912) loop == 768 slots of the 536-byte table.
    for (int k = 0; k < count; ++k) {
        u8 role = slots[k].role; // byte_12CE912 (+0x16A)
        if (role == 6 || role == 7) {
            send(slots[k].objectId, subject, arg, ctx);
            ++sent;
        }
    }
    return sent;
}

} // namespace guild::world
