#pragma once
// MeisterAi trade-logistics planner — the deferred-from-meister_trade decision
// cores for transporter management and city-wide stock balancing (gilde.exe).
//
//   * CollectTransporters   0x45e71c — twice-daily (odd hour) transporter audit:
//       reroute "lost" carts (carts whose route building is gone) home, then
//       decide whether to BUY a new cart based on cart count vs. the AI's
//       transporter quota, an RNG gate, and an affordability check.
//   * RefillTavernStock     0x45f0a0 — per-tavern-slot restock roll: a slot whose
//       fill level falls below 40 + RandomModulo(0x20) is topped up (SlotReset28
//       op 5, scheduled +10 minutes out, amount = 100 - fillLevel).
//   * BalanceCityGoods      0x4c763c — periodic per-person wealth top-up: every
//       person of class 4 (every tick) or class 2 (on its turn-phase) below the
//       wealth floor (32000 + 8000*difficulty) is granted the shortfall via
//       EnqueueCmd15, flushing every 32 grants.
//
// The full bodies walk the scene tree, the He handler table, the 768-person array
// and the network packet-status ring; those engine reads are injected through
// MeisterTrade2Env and commands go through a hook so the RULES are testable. The
// transporter cart item ids (308/309/310) and the RNG/threshold constants are
// recovered byte-for-byte.
#include <vector>

#include "guild/common/types.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// CollectTransporters
// ---------------------------------------------------------------------------
// Transporter cart item ids (scene node type 29). The AI buys one of these when
// short of carts. 308 = base cart, 309 = better, 310 = best (mule/wagon tiers).
constexpr u16 kCartIdBase = 308;
constexpr u16 kCartIdMid  = 309;
constexpr u16 kCartIdHigh = 310;
constexpr int kCartBuyRollMod = 0x2EE; // RandomModulo(750): fires when result < 2
constexpr int kCartBuyRollHit = 2;

// A command the transporter audit emits (collapsed; the original calls
// QueueRequestSlotReset28 for a reroute and EnqueueCmd15 + QueueRequest17 for a
// purchase).
enum class TransporterCmd {
    RerouteLost, // a lost cart is sent from `fromBuilding` back to home
    BuyCart,     // buy one cart of `goodId` for `cost` charged to `account`
};
struct TransporterCommand {
    TransporterCmd kind;
    u16 goodId = 0;
    i32 cost = 0;
    i32 account = 0;
    i32 fromBuilding = 0;
};

// The faction transporter state the buy decision reads.
struct TransporterState {
    int  aiType = 0;          // AiPlayer +0 (9 = caravan/Karawane class)
    int  quota = 0;           // AiPlayer +583 transporter quota
    int  cartCount = 0;       // # transporter nodes owned (v49)
    int  highCartCount = 0;   // # of those that are id 310 (v48)
    bool busy = false;        // (+456 & 8) — a purchase already queued this turn
    int  budget = 0;          // AiPlayer +440 available funds
    u8   ownerClass = 0;      // byte_12CE912[owner] (6/7 = human-controlled gate)
    bool flag436_4 = false;   // (+436 & 4) — secondary purchase-enable flag
    i32  ownerAccount = 0;    // dword_12CE914[owner] command target
};

// gilde.exe 0x45e71c (purchase decision) — decide which cart (if any) to buy.
// `price_of` returns ComputeMarketPrice(cartId, 100); `roll_750` is one
// RandomModulo(0x2EE) draw, `roll_8` one RandomModulo(8) draw (only the type-9
// 309/310 branch consumes roll_8). Returns the cart id to buy (308/309/310) or 0
// for none. Faithful to the branch structure:
//   - aiType==9 && cartCount!=highCartCount: if highCartCount>0 buy 309/310
//     (310 iff roll_8>=4), else buy 310; gate quota>cartCount, roll<2, 2*price<budget.
//   - aiType!=9 && cartCount>0: gate ownerClass not 6/7 OR flag436_4; buy 309.
//   - else: buy 308 (the fallback restock).
// Sets *cost = price (the EnqueueCmd15 amount) when a buy fires.
u16 TransporterBuyDecision(const TransporterState& st,
                           float (*price_of)(u16 cartId),
                           int roll_750, int roll_8, i32* cost);

// gilde.exe 0x45e71c (top-level gate) — the audit only runs on odd hours; on even
// hours it clears the busy flag and does nothing. Returns true if the audit body
// should run this tick (hour is odd and the +436 high bit is clear).
bool TransporterAuditRuns(int hour, bool flag436_high);

// ---------------------------------------------------------------------------
// RefillTavernStock
// ---------------------------------------------------------------------------
// gilde.exe 0x45f0a0 — per-slot restock decision. `fillLevel` is the slot's fill
// byte (+18). `roll_32` is one RandomModulo(0x20) draw. Refill fires when
// fillLevel < 40 + roll_32; the restock amount is 100 - fillLevel. Returns the
// amount to add (0 = no refill).
int TavernRefillAmount(int fillLevel, int roll_32);
constexpr int kTavernRefillBase = 40;     // 40 + RandomModulo(0x20)
constexpr int kTavernRefillTarget = 100;  // amount = 100 - fillLevel
constexpr int kTavernRefillDelayMin = 10; // GameTime_Advance(.., 0, 10, 0)

// ---------------------------------------------------------------------------
// BalanceCityGoods
// ---------------------------------------------------------------------------
// gilde.exe 0x4c763c — per-person wealth top-up.
//   floor = 32000 + 8000 * difficulty   (dword_63C744 = difficulty level)
// A person of class 4 always qualifies; a person of class 2 qualifies only when
// (tick % 4) == (personId & 3) (a staggered 1-in-4 schedule); class 5 stops the
// whole scan. Returns the grant amount (floor - held) when held < floor, else 0.
struct BalanceGrant {
    i32 personAccount = 0;
    i32 amount = 0;
};
int CityWealthFloor(int difficulty);
constexpr int kWealthFloorBase = 32000;
constexpr int kWealthFloorPerDifficulty = 8000;

// One person record the balance pass reads.
struct BalancePerson {
    u8  personClass = 0; // byte_12CE912 (2/4/5)
    i32 personId = 0;    // dword_12CE914 column (also the &3 schedule key)
    i32 held = 0;        // Person_SumCurrencyHeld
    i32 account = 0;     // command target (dword_12CE914)
};

// gilde.exe 0x4c763c (per-person rule) — decide the grant for one person.
//   classQualifies: class 4 -> always; class 2 -> (tick % 4)==(personId & 3);
//   class 5 -> caller should STOP the scan (returns -1 in *stop); else skip.
// Returns the grant amount (>0) to emit, or 0 for no grant. `*stop` set to true
// when a class-5 person is hit (the original `break`).
int BalancePersonGrant(const BalancePerson& p, int tick, int floor, bool* stop);

// Runs the full balance scan over `people`, returning the grant commands in order
// (stopping at the first class-5 person). Mirrors the 32-grant flush boundary
// only in that the running list is single-valued (flush is a net keep-alive).
std::vector<BalanceGrant> BalanceCityGoods(const std::vector<BalancePerson>& people,
                                           int tick, int difficulty);

} // namespace guild::ai
