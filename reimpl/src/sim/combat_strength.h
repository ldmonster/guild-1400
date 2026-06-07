#pragma once
// gilde.exe — Combat "strength" functions @0x48d160 / @0x48d318 — VERIFIED to be
// ECONOMY (raid-loot / market transfer), NOT combat-strength scoring.
// (Combat-strength scoring is VIBE_Combat_GetSoundRangeScale @0x485dc0, already in
// combat_battle.cpp.) namespace guild::sim. MODULE: combat (prefix VIBE_Combat_*).
//
// VERIFICATION. Both functions:
//   * QueryFind scene objects (the source/target production stockpiles),
//   * roll a per-stockpile quantity with the cutscene LCG,
//   * EMIT a market-sell command per ware (VIBE_Command_QueueRequest17) and a
//     bulk cash command (VIBE_Command_EnqueueCmd15),
//   * look up each ware's cached MARKET PRICE (VIBE_Building_LookupCachedMarketPrice),
//   * accumulate the cash value into the side's FAMILY/MONEY record (+7 dword of
//     VIBE_Person_GetFamilyRecord).
// There is NO HP / weapon / win-lose logic here — it is the loot/booty transfer
// that runs when a raid resolves. Named *Strength only because the IDA auto-namer
// mislabelled them; the cash total is the "strength" (worth) of the haul.
//
// Determinism: the quantity rolls use the cutscene LCG (CutsceneRng::RandInt),
// exactly as the originals (VIBE_Cutscene_RandInt @0x4ac9e8). The market sells /
// cash credit are mutations -> routed through the command sink (mock). The market
// price lookup is a forward-declared building-module leaf supplied by the caller.
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_types.h"

#include <functional>
#include <vector>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants (the .rdata doubles):
//   dbl_61B9F4 = 0.001   (attacker base-cash roll scale)
//   dbl_61B9FC = 0.07    (attacker base-cash roll bias)
//   dbl_61BA04 = 0.01    (attacker per-ware quantity scale)
//   dbl_61BA0C = 0.5     (defender bulk-cash fraction when no commander present)
// ---------------------------------------------------------------------------
constexpr double kLootBaseCashScale = 0.001;  // dbl_61B9F4
constexpr double kLootBaseCashBias  = 0.07;   // dbl_61B9FC
constexpr double kLootWareQtyScale  = 0.01;   // dbl_61BA04
constexpr double kLootDefenderFrac  = 0.5;    // dbl_61BA0C

// A single ware in a raided stockpile: its ware-type id + the stockpile's
// quantity scalar (the *(node+7) "worth" the original multiplies the roll by).
struct LootWare {
    i32 wareTypeId = 0;   // *node (the ware id / type word)
    int quantity   = 0;   // *(int*)(node+7) — stockpile quantity / worth
    i32 ownerId    = 0;   // *(node+3) — owner person id (defender side)
};

// The market-sell / cash command sink (the lockstep emissions the originals make:
// QueueRequest17 per ware, EnqueueCmd15 for the cash). Forward-declared so the
// loot transfer is testable. A host wires this to the real Command_* builders.
class ILootCommandSink {
public:
    virtual ~ILootCommandSink() = default;
    // VIBE_Command_QueueRequest17(sellerId, buyerId, qty, wareType): a market sell.
    virtual void OnSellWare(i32 sellerId, i32 buyerId, int qty, i32 wareType) = 0;
    // VIBE_Command_EnqueueCmd15(value): credit a bulk cash amount to the haul.
    virtual void OnCashCredit(int value) = 0;
};

// The cached market price provider (VIBE_Building_LookupCachedMarketPrice @0x58f6b8,
// owned by the building module). Returns the unit market price of `wareType`.
using MarketPriceFn = std::function<double(i32 wareType)>;

// gilde.exe 0x48d160 — VIBE_Combat_ComputeAttackerStrength (raid-loot of the
// ATTACKER's spoils). The data-flow rule:
//   * cashTotal  = (int)((RandInt(50) * 0.001 + 0.07) * commanderCash);  // base
//     EnqueueCmd15(cashTotal);
//   * for each ware in the attacker's source stockpile:
//        qty = max(1, (int)((RandInt(20) + 80) * 0.01 * ware.quantity));
//        QueueRequest17(targetSellerId, attackerBuyerId, qty, ware.wareTypeId);
//        cashTotal += (int)(marketPrice(ware.wareTypeId) * qty);
//   * credit cashTotal to the attacker's family record (+7).
// `commanderCash` is VIBE_Person_SumCurrencyHeld of the squad commander; `wares`
// is the attacker source stockpile; `targetSellerId`/`attackerBuyerId` are the
// two side ids (v21 / v4 node ids). Returns the accumulated cash total.
struct LootResult {
    int cashTotal = 0;        // the v22 / v18 accumulator credited to the family
    int familyCredited = -1;  // family-record money after credit (-1 if no record)
};
LootResult ComputeAttackerStrength(int commanderCash, const std::vector<LootWare>& wares,
                                   i32 targetSellerId, i32 attackerBuyerId,
                                   bool hasFamilyRecord, int familyMoneyBefore,
                                   const MarketPriceFn& marketPrice, CutsceneRng& rng,
                                   ILootCommandSink* sink = nullptr);

// gilde.exe 0x48d318 — VIBE_Combat_ComputeDefenderStrength (raid-loot of the
// DEFENDER's stockpiles, one per production object the squad holds). For each
// production object (squad.productionObjects, -1 == empty):
//   * for each ware in that object's stockpile:
//        qty = max(1, (int)(ware.quantity * rate));            // rate == a3
//        QueueRequest17(commanderSellerId|-1, ware.ownerId, qty, ware.wareTypeId);
//        cashTotal += (int)(marketPrice(ware.wareTypeId) * qty);
//   * if there is NO commander (v5 == 0): cashTotal = (int)(cashTotal * 0.5);
//        EnqueueCmd15(cashTotal);
//   * credit cashTotal to the ATTACKER family record (dword_6311E8, +7).
// `rate` is the a3 multiplier (currency/quantity rate). `hasCommander` mirrors v5
// (the resolved commander node) — false applies the 0.5 fraction + the cash cmd.
// `stockpiles` is the per-production-object ware lists. Returns the total.
LootResult ComputeDefenderStrength(const std::vector<std::vector<LootWare>>& stockpiles,
                                   float rate, i32 commanderSellerId, bool hasCommander,
                                   bool hasFamilyRecord, int familyMoneyBefore,
                                   const MarketPriceFn& marketPrice,
                                   ILootCommandSink* sink = nullptr);

} // namespace guild::sim
