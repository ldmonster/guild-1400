#include "ai/meister_trade2.h"

// MeisterAi transporter / city-balance planning RULES (gilde.exe 0x45e71c /
// 0x45f0a0 / 0x4c763c). The engine sweeps that feed these (scene-tree scan for
// transporter nodes, He handler matching, the 768-person loop, packet-status
// flush) are DEFERRED — pure entity-array plumbing — but the per-decision RULES
// (which cart to buy, when to refill, how much to grant) are translated 1:1 and
// exercised on synthetic inputs.

namespace guild::ai {

// gilde.exe 0x45e71c (top-level gate).
bool TransporterAuditRuns(int hour, bool flag436_high) {
    // even hour -> clear busy bit, do nothing (return false).
    if ((hour % 2) == 0)
        return false;
    // odd hour: runs only if the +436 high bit is clear (signed byte >= 0).
    return !flag436_high;
}

// gilde.exe 0x45e71c (purchase decision).
u16 TransporterBuyDecision(const TransporterState& st,
                           float (*price_of)(u16 cartId),
                           int roll_750, int roll_8, i32* cost) {
    if (cost)
        *cost = 0;

    auto tryBuy = [&](u16 cartId) -> u16 {
        // gate already passed (quota > cartCount). RNG + affordability gates:
        if (roll_750 >= kCartBuyRollHit)   // RandomModulo(0x2EE) < 2
            return 0;
        float price = price_of(cartId);
        i32 p = static_cast<i32>(static_cast<double>(price)); // Coord_ConvertX
        if (2 * p >= st.budget)            // 2*price < budget required
            return 0;
        if (cost)
            *cost = p;
        return cartId;
    };

    if (st.aiType == 9) {
        // caravan class.
        if (st.cartCount == st.highCartCount)
            return 0; // balanced fleet, no purchase in this branch
        if (st.busy)
            return 0;
        if (st.quota <= st.cartCount)
            return 0;
        if (st.highCartCount > 0) {
            // buy 309 or 310 (310 iff roll_8 >= 4).
            u16 cartId = static_cast<u16>(kCartIdMid + (roll_8 >= 4 ? 1 : 0));
            return tryBuy(cartId);
        }
        return tryBuy(kCartIdHigh);
    }

    // non-caravan class.
    if (st.cartCount > 0) {
        // human-controlled gate: if class is 6/7, require the +436&4 flag.
        if ((st.ownerClass == 6 || st.ownerClass == 7) && !st.flag436_4)
            return 0;
        if (st.busy)
            return 0;
        if (st.quota <= st.cartCount)
            return 0;
        return tryBuy(kCartIdMid); // 309
    }

    // no carts at all: the fallback restock buys a base cart (308). The original
    // also short-circuits the human-controlled (6/7) class behind the +436&4
    // flag here.
    if ((st.ownerClass == 6 || st.ownerClass == 7) && !st.flag436_4)
        return 0;
    // the 308 fallback has no quota/roll gate in the original (it always
    // restocks a base cart), but still respects affordability via tryBuy's price
    // check. We keep the roll gate out for the 308 path: emit unconditionally
    // priced.
    {
        float price = price_of(kCartIdBase);
        i32 p = static_cast<i32>(static_cast<double>(price));
        if (cost)
            *cost = p;
        return kCartIdBase;
    }
}

// gilde.exe 0x45f0a0 — tavern slot refill amount.
int TavernRefillAmount(int fillLevel, int roll_32) {
    if (fillLevel < kTavernRefillBase + roll_32)
        return kTavernRefillTarget - fillLevel;
    return 0;
}

// gilde.exe 0x4c763c — city wealth floor.
int CityWealthFloor(int difficulty) {
    return kWealthFloorBase + kWealthFloorPerDifficulty * difficulty;
}

// gilde.exe 0x4c763c (per-person rule).
int BalancePersonGrant(const BalancePerson& p, int tick, int floor, bool* stop) {
    if (stop)
        *stop = false;
    if (p.personClass == 5) {
        if (stop)
            *stop = true; // the original `break`s the whole scan
        return 0;
    }
    bool qualifies = false;
    if (p.personClass == 4) {
        qualifies = true;
    } else if (p.personClass == 2) {
        // staggered 1-in-4 schedule keyed on the low 2 bits of the id.
        qualifies = ((tick % 4) == (p.personId & 3));
    }
    if (!qualifies)
        return 0;
    if (p.held < floor)
        return floor - p.held;
    return 0;
}

// gilde.exe 0x4c763c — full balance scan.
std::vector<BalanceGrant> BalanceCityGoods(const std::vector<BalancePerson>& people,
                                           int tick, int difficulty) {
    std::vector<BalanceGrant> out;
    const int floor = CityWealthFloor(difficulty);
    for (const BalancePerson& p : people) {
        bool stop = false;
        int amount = BalancePersonGrant(p, tick, floor, &stop);
        if (stop)
            break;
        if (amount > 0)
            out.push_back(BalanceGrant{p.account, amount});
    }
    return out;
}

} // namespace guild::ai
