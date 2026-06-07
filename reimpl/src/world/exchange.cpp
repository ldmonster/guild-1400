#include "world/exchange.h"

#include <cmath>

namespace guild::world {

namespace {
std::vector<i32> g_rateTable;     // dword_649A88[]
ExchangeCmdHook  g_cmdHook = nullptr;
void*            g_cmdCtx  = nullptr;

i32 Trunc(double x) { return static_cast<i32>(std::trunc(x)); }
} // namespace

void ExchangeSetRateTable(const std::vector<i32>& table) { g_rateTable = table; }

// gilde.exe 0x58f1d0 — VIBE_Money_PriceByRate: dword_649A88[cur] * amount.
i32 ExchangePriceByRate(i32 amount, u16 currency) {
    if (currency < g_rateTable.size())
        return g_rateTable[currency] * amount;
    return 0;
}

void ExchangeSetCmdHook(ExchangeCmdHook hook, void* ctx) {
    g_cmdHook = hook;
    g_cmdCtx = ctx;
}
void ExchangeEmit(const ExchangeCommand& cmd) {
    if (g_cmdHook)
        g_cmdHook(cmd, g_cmdCtx);
}

// gilde.exe 0x51cbdc — VIBE_Exchange_ShowCourierDialog (courier fee rule).
//   v8   = PriceByRate(rate, amountCurrency);               // base
//   v28  = (double)v8 * 0.03;                               // span (dbl_6220D0)
//   if ( PriceByRate(1, amountCurrency) >= v28 )            // min-fee clamp
//       v26 = (float)PriceByRate(1, amountCurrency);
//   else
//       v26 = v28;
//   QueueRequest17(-1, playerAccount, v8, amountCurrency, ..);  // charge base
//   v12 = (double)v8 - v26;  ... QueueRequestSlotReset28(net);  // deliver net
CourierResult ExchangeCourier(i32 rate, u16 amountCurrency, i32 playerAccount,
                              bool commit) {
    CourierResult r;
    r.base = ExchangePriceByRate(rate, amountCurrency);
    double span = static_cast<double>(r.base) * kCourierFactor;
    i32 minA = ExchangePriceByRate(1, amountCurrency);
    double fee = (static_cast<double>(minA) >= span)
                     ? static_cast<double>(ExchangePriceByRate(1, amountCurrency))
                     : span;
    r.fee = static_cast<i32>(static_cast<float>(fee)); // v26 is a float in the orig
    r.net = Trunc(static_cast<double>(r.base) - static_cast<float>(fee));

    if (commit) {
        ExchangeCommand charge;
        charge.payer = -1;
        charge.recipient = playerAccount;
        charge.amount = r.base;
        charge.field = amountCurrency;
        ExchangeEmit(charge);

        ExchangeCommand deliver;
        deliver.payer = -1;
        deliver.recipient = playerAccount;
        deliver.amount = r.net;
        deliver.field = amountCurrency;
        ExchangeEmit(deliver);
    }
    return r;
}

// gilde.exe 0x51bb4c — goods-exchange trade arithmetic (one accepted swap).
//   v112 = PriceByRate(takeQty, takeCurrency);              // counterparty value
//   v117 = playerCash * 0.01;                               // dbl_622068
//   if (homeCity == playerCity) fee = 0;
//   else { v117 = v112 * v117;                              // span
//          if (PriceByRate(1, takeCurrency) >= v117)
//              fee = PriceByRate(1, giveCurrency);
//          else fee = v117; }
//   if (PriceByRate(giveQty, giveCurrency) < v112) reject;  // cannot afford
//   else commit four QueueRequest17 legs (see header).
GoodsTradeResult ExchangeGoodsTrade(const GoodsTradeInput& in, bool commit) {
    GoodsTradeResult r;
    double value = static_cast<double>(ExchangePriceByRate(in.takeQty, in.takeCurrency));
    r.giveValue = static_cast<i32>(value);

    double feeBase = static_cast<double>(in.playerCash) * kExchangeSpread;
    double fee = 0.0;
    if (!in.sameCity) {
        double span = value * feeBase;
        i32 minA = ExchangePriceByRate(1, in.takeCurrency);
        fee = (static_cast<double>(minA) >= span)
                  ? static_cast<double>(ExchangePriceByRate(1, in.giveCurrency))
                  : span;
    }
    r.fee = static_cast<i32>(static_cast<float>(fee));

    // Affordability gate: the player's give-slot value must cover the take value.
    i32 affordable = ExchangePriceByRate(in.giveQty, in.giveCurrency);
    if (static_cast<double>(affordable) < value) {
        r.accept = false;
        return r;
    }
    r.accept = true;
    if (!commit)
        return r;

    i32 v98 = Trunc(value);
    i32 takeValue = ExchangePriceByRate(in.takeQty, in.takeCurrency);

    // (1) take good in:   QueueRequest17(-1, playerCity, v98, giveGood, ..)
    ExchangeEmit({-1, in.playerCity, v98, static_cast<u16>(in.giveGood), 0});
    // (2) give good out:  QueueRequest17(playerCity, -1, trunc(value-fee), takeGood)
    ExchangeEmit({in.playerCity, -1, Trunc(value - static_cast<float>(fee)),
                  static_cast<u16>(in.takeGood), 0});
    // (3) pay:            QueueRequest17(-1, playerAccount, takeValue, takeGood)
    ExchangeEmit({-1, in.playerAccount, takeValue, static_cast<u16>(in.takeGood), 0});
    // (4) receive:        QueueRequest17(playerAccount, -1, trunc(v98+fee), giveGood)
    ExchangeEmit({in.playerAccount, -1, Trunc(static_cast<double>(v98) + static_cast<float>(fee)),
                  static_cast<u16>(in.giveGood), 0});
    return r;
}

// gilde.exe 0x51ca40 — VIBE_Exchange_ShowFeesDialog (commit on widget 1210):
//   DataPtr_buy  = GetDataPtr(buyField);   AppendDeltaField(.., field 105)
//   DataPtr_sell = GetDataPtr(sellField);  AppendDeltaField(.., field 109)
//   QueueRequestState22();
void ExchangeApplyFees(i32 objectId, const FeeEdit& fees, bool commit) {
    if (!commit)
        return;
    ExchangeEmit({objectId, objectId, fees.buyFee, 105, 0});
    ExchangeEmit({objectId, objectId, fees.sellFee, 109, 0});
}

} // namespace guild::world
