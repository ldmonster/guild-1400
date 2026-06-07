#pragma once
// Goods/currency exchange ("Wechsel") rules — the contor exchange dialogs core
// (gilde.exe). Recovers the FEE + per-good buy/sell arithmetic from the three
// "Geldleihe/Wechsel" dialogs; the GUI shell (form/window/slot layout, drag,
// rendering) is DEFERRED and listed in the report. Translated rules:
//
//   VIBE_Exchange_ShowGoodsExchangeDialog 0x51bb4c — the goods-exchange trade
//       arithmetic (fee on a foreign-currency move + the four commit transfers).
//   VIBE_Exchange_ShowFeesDialog          0x51ca40 — set the two exchange-fee
//       fields (player+105 / player+109) via a delta packet.
//   VIBE_Exchange_ShowCourierDialog       0x51cbdc — courier fee rule (rate *
//       0.03, clamped to a per-unit minimum) + the two commit transfers.
//   VIBE_Money_PriceByRate                0x58f1d0 — dword_649A88[cur] * amount.
//
// Mutations route through the exchange command hook (the originals emit
// VIBE_Command_QueueRequest17 and a delta packet). A per-currency rate table is
// injected (the live image table is dword_649A88).
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP constants (get_bytes; see comments).
// ===========================================================================
constexpr double kExchangeSpread = 0.01;  // dbl_622068 (0x3F847AE147AE147B)
constexpr double kCourierFactor  = 0.03;  // dbl_6220D0 (0x3F9EB851EB851EB8)

// ===========================================================================
// Per-currency rate table (gilde.exe dword_649A88[]). PriceByRate(amount, cur)
// = table[cur] * amount. The static image is runtime-seeded; tests inject it.
// ===========================================================================
void ExchangeSetRateTable(const std::vector<i32>& table);
// gilde.exe 0x58f1d0 — VIBE_Money_PriceByRate(amount@<eax>, cur@<dx>):
//   return dword_649A88[cur] * amount;
i32 ExchangePriceByRate(i32 amount, u16 currency);

// ===========================================================================
// Command hook (lockstep). The exchange dialogs commit each leg of a trade with
// VIBE_Command_QueueRequest17(payer, recipient, amount, goodOrCurrency, ..).
// ===========================================================================
struct ExchangeCommand {
    i32 payer = 0;       // -1 == treasury/bank sink
    i32 recipient = 0;   // -1 == treasury/bank sink
    i32 amount = 0;
    u16 field = 0;       // good id (goods dialog) / currency id (courier)
    int currency = 0;
};
using ExchangeCmdHook = void (*)(const ExchangeCommand& cmd, void* ctx);
void ExchangeSetCmdHook(ExchangeCmdHook hook, void* ctx);
void ExchangeEmit(const ExchangeCommand& cmd);

// ===========================================================================
// gilde.exe 0x51cbdc — VIBE_Exchange_ShowCourierDialog (the courier fee rule).
//   base = PriceByRate(rate, amountCurrency);              // v8
//   span = (double)base * 0.03;                             // dbl_6220D0
//   minA = PriceByRate(1, amountCurrency);                  // home currency
//   fee  = (minA >= span) ? (double)PriceByRate(1, amountCurrency) : span;
//   // (the original re-reads PriceByRate(1, srcCurrency) on the >= branch)
//   net  = trunc((double)base - fee);                       // delivered amount
// Returns {base, fee, net}. `commit` emits the two legs the original queues:
//   QueueRequest17(-1, playerAccount, base, amountCurrency, ..)   (charge)
//   QueueRequestSlotReset28(net, ...)                             (deliver)
struct CourierResult {
    i32 base = 0;
    i32 fee = 0;
    i32 net = 0;
};
CourierResult ExchangeCourier(i32 rate, u16 amountCurrency, i32 playerAccount,
                              bool commit);

// ===========================================================================
// gilde.exe 0x51bb4c — VIBE_Exchange_ShowGoodsExchangeDialog (the trade math for
// one accepted good-for-good swap). The dialog matches a "give" slot (player's
// good) against a "take" slot (counterparty's good) and, when the player is in a
// FOREIGN city, charges an exchange fee on the move. Rule (slot v120==1):
//   giveValue = PriceByRate(giveQty, giveCurrency);               // v112
//   feeBase   = playerCash * 0.01;                                // dbl_622068
//   if (homeCity == playerCity) fee = 0;
//   else { span = giveValue * feeBase;
//          minA = PriceByRate(1, giveCurrency);
//          fee  = (minA >= span) ? PriceByRate(1, takeCurrency) : span; }
//   if (PriceByRate(takeQty, takeCurrency) < giveValue) -> reject (cannot afford)
//   else commit:
//     QueueRequest17(-1, playerCity, trunc(giveValue), giveGood, ..)  // take good in
//     QueueRequest17(playerCity, -1, trunc(giveValue-fee), takeGood, ..) // give good out
//     QueueRequest17(-1, playerAccount, takeValue, takeGood, ..)      // pay
//     QueueRequest17(playerAccount, -1, trunc(giveValue+fee), giveGood, ..) // receive
// We expose the deterministic numbers (giveValue, fee, accept) so they can be
// golden-checked; on accept+commit the four legs are emitted.
struct GoodsTradeInput {
    i32 giveQty = 0;        i32 giveGood = 0;   u16 giveCurrency = 0;
    i32 takeQty = 0;        i32 takeGood = 0;   u16 takeCurrency = 0;
    i32 playerCash = 0;     // player+109
    i32 playerAccount = 0;  // player+1
    i32 playerCity = 0;     // dword_12CE914[..] city account
    bool sameCity = false;  // word_63CC5C == player+39 (home == current)
};
struct GoodsTradeResult {
    i32  giveValue = 0;   // trunc-able giveValue (PriceByRate(giveQty, giveCur))
    i32  fee = 0;
    bool accept = false;  // takeValue >= giveValue
};
GoodsTradeResult ExchangeGoodsTrade(const GoodsTradeInput& in, bool commit);

// ===========================================================================
// gilde.exe 0x51ca40 — VIBE_Exchange_ShowFeesDialog. On confirm (widget 1210) it
// reads the two edited fee fields and writes them back to the object via a delta
// packet (fields 105 and 109). This recovers the field map + commit shape.
// ===========================================================================
struct FeeEdit {
    i32 buyFee = 0;   // object+105
    i32 sellFee = 0;  // object+109
};
// `commit` emits the delta as two ExchangeCommands tagged with field 105 / 109
// (payer = object id, amount = the fee value).
void ExchangeApplyFees(i32 objectId, const FeeEdit& fees, bool commit);

} // namespace guild::world
