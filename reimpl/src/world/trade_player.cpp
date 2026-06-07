#include "world/trade_player.h"

#include <cmath>

namespace guild::world {

namespace {
std::vector<i32> g_rateByCurrency;   // dword_649A88[]
std::vector<i32> g_cityCurrency;     // (cityRecord+82)>>16 per city
TradeMoneyHook   g_moneyHook = nullptr;
void*            g_moneyCtx  = nullptr;

i32 Trunc(double x) { return static_cast<i32>(std::trunc(x)); }
} // namespace

void TradeSetCurrencyRateTable(const std::vector<i32>& t) { g_rateByCurrency = t; }
void TradeSetCityCurrencyTable(const std::vector<i32>& t) { g_cityCurrency = t; }

// rate = dword_649A88[ dword_13CD6F2[189*city] >> 16 ]; identity (1) when unset.
i32 TradeCityRate(u8 city) {
    i32 currencyId = 0;
    if (city < g_cityCurrency.size())
        currencyId = g_cityCurrency[city];
    if (currencyId >= 0 && static_cast<size_t>(currencyId) < g_rateByCurrency.size()) {
        i32 r = g_rateByCurrency[currencyId];
        return r != 0 ? r : 1;
    }
    return 1;
}

// gilde.exe 0x58f14c — VIBE_Money_ConvertToDisplayCoord.
//   v2 = (double)a1 / (double)rate + 0.5; VIBE_Coord_ConvertX(); return (int)v2;
// (VIBE_Coord_ConvertX is round-toward-zero == trunc.)
i32 MoneyConvertToDisplayCoord(i32 amount, u8 city) {
    double v = static_cast<double>(amount) / static_cast<double>(TradeCityRate(city))
             + kMoneyRoundBias;
    return Trunc(v);
}

// gilde.exe 0x58f1dc — VIBE_Money_DivideByRate: a1 / dword_649A88[a2].
i32 MoneyDivideByRate(i32 amount, u16 currencyId) {
    if (currencyId < g_rateByCurrency.size()) {
        i32 r = g_rateByCurrency[currencyId];
        if (r != 0)
            return amount / r;
    }
    return amount;  // rate 1 fallback
}

// gilde.exe 0x51bae8 — VIBE_Trade_FindSelectedSlotIndex.
int TradeFindSelectedSlotIndex(bool clickActive, i32 clickedId,
                               const std::vector<i32>& slots) {
    if (!clickActive)
        return -1;
    for (int i = 0; i < 16; ++i) {
        if (static_cast<size_t>(i) >= slots.size())
            return -1;
        i32 id = slots[i];
        if (id != -1 && clickedId == id)
            return i;
    }
    return -1;
}

void TradeSetMoneyHook(TradeMoneyHook hook, void* ctx) {
    g_moneyHook = hook;
    g_moneyCtx = ctx;
}
void TradeEmitMoney(const TradeMoneyCommand& cmd) {
    if (g_moneyHook)
        g_moneyHook(cmd, g_moneyCtx);
}

// gilde.exe 0x519b14 — VIBE_WineCellar_ShowBuyDialog (BUY rule core).
//   v19 = VIBE_Money_MultiplyByRate(itemValue, city);
//   if (playerCash >= v19) EnqueueCmd15(playerAccount, sellerAccount,
//                                       playerCash - v19, city);
//   else                   EnqueueCmd15(sellerAccount, playerAccount,
//                                       v19 - playerCash, city);
BuyResult WineCellarBuy(const BuyInput& in, bool commit) {
    BuyResult r;
    // MultiplyByRate == itemValue * rate (the reused world/amt model; here the
    // value is already in the displayed currency, so rate is the city scalar).
    r.cost = in.itemValue * TradeCityRate(in.city);
    r.afford = in.playerCash >= r.cost;

    if (commit) {
        TradeMoneyCommand cmd;
        cmd.currency = in.city;
        if (r.afford) {
            cmd.payer = in.playerAccount;
            cmd.recipient = in.sellerAccount;
            cmd.amount = in.playerCash - r.cost;
        } else {
            cmd.payer = in.sellerAccount;
            cmd.recipient = in.playerAccount;
            cmd.amount = r.cost - in.playerCash;
        }
        TradeEmitMoney(cmd);
    }
    return r;
}

// gilde.exe 0x50c5d4 — VIBE_Trade_RegisterEinkaufContact.
EinkaufResult TradeRegisterEinkaufContact(const EinkaufContext& in,
                                          EinkaufOpenHook openHook, void* ctx) {
    EinkaufResult r;
    // Foreign-city gate: word_63CC5C != *(obj+39).
    if (in.homeCity == in.currentCity)
        return r;

    // First present contact in the fixed probe order (EINKAUF, _METALL,
    // _SKRIPTE, _HOLZ, _STEIN). The original short-circuits on the first hit.
    for (int i = 0; i < 5; ++i) {
        if (in.present[i]) {
            r.matched = static_cast<EinkaufContact>(i);
            break;
        }
    }

    // Open the panel (mode 1) only if the matched contact is the clicked widget.
    if (r.matched != EinkaufContact::None && in.clickedIsContact) {
        r.openPanel = true;
        if (openHook)
            openHook(ctx);
    }
    return r;
}

} // namespace guild::world
