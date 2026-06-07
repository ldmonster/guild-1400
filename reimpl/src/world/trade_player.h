#pragma once
// ===========================================================================
// trade_player.{h,cpp} — the remaining player-facing Trade money/coord rules and
// the player BUY resolution core (gilde.exe). MODULE: the player Trade bodies not
// already covered by sim/trade_sell (the SELL side) or world/exchange.
// ===========================================================================
//
// What is recovered here byte-for-byte:
//
//   * VIBE_Money_ConvertToDisplayCoord 0x58f14c — divide an amount by the city
//     currency rate, round-to-nearest (+0.5), truncate. The home-currency "buy
//     price displayed in Gulden" primitive.
//   * VIBE_Money_DivideByRate          0x58f1dc — amount / rateTable[currencyId].
//   * VIBE_Trade_FindSelectedSlotIndex 0x51bae8 — the clicked trade-slot index in
//     the 16-entry, 7-dword-stride slot table.
//   * VIBE_WineCellar_ShowBuyDialog    0x519b14 — the player BUY rule core: the
//     per-unit cost = MultiplyByRate(value, currency), the affordability gate, and
//     the two-leg balance transfer the dialog commits (EnqueueCmd15).
//   * VIBE_Trade_RegisterEinkaufContact 0x50c5d4 — the "Einkauf" buy-contact
//     router: maps a contact object name (EINKAUF / _METALL / _SKRIPTE / _HOLZ /
//     _STEIN) to the trade-panel open action, gated on being in a FOREIGN city.
//
// The currency rate lookup the Money helpers use is, in the live image,
//   dword_649A88[ cityRecord[city]+82 >> 16 ]   (per-city currency object id ->
// per-currency rate). We model it as a settable per-currency rate table joined
// with a per-city currency-id table so the arithmetic is host/test controlled.
// Mutations route through a settable command hook (the originals emit
// VIBE_Command_EnqueueCmd15 / the trade panel open thunks).
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP constant (get_bytes): the +0.5 round-to-nearest bias both
// ConvertToDisplayCoord and the money formatter add before truncating.
// ===========================================================================
constexpr double kMoneyRoundBias = 0.5;  // dbl_6268DC (0x3FE0000000000000)

// ===========================================================================
// Currency rate model. The live image reads
//   rate = dword_649A88[ dword_13CD6F2[189*city] >> 16 ]
// i.e. the per-city currency object id (city record +82, high word) indexes a
// per-currency rate table. We surface both as settable tables; with neither set,
// the rate is 1 (identity — the home-currency case the dialogs assume).
// ===========================================================================
void TradeSetCurrencyRateTable(const std::vector<i32>& rateByCurrencyId);
// city -> currency object id (the (cityRecord+82)>>16 column). Index = city id.
void TradeSetCityCurrencyTable(const std::vector<i32>& currencyIdByCity);
// The resolved per-city multiplier rateTable[ cityCurrency[city] ] (>=1).
i32 TradeCityRate(u8 city);

// gilde.exe 0x58f14c — VIBE_Money_ConvertToDisplayCoord(amount@<eax>, city@<dl>):
//   v = (double)amount / rate(city) + 0.5;  return trunc(v);
// Round-to-nearest division of a foreign amount into the displayed home value.
i32 MoneyConvertToDisplayCoord(i32 amount, u8 city);

// gilde.exe 0x58f1dc — VIBE_Money_DivideByRate(amount@<eax>, currencyId@<dx>):
//   return amount / rateTable[currencyId];   (integer divide, truncating)
i32 MoneyDivideByRate(i32 amount, u16 currencyId);

// ===========================================================================
// gilde.exe 0x51bae8 — VIBE_Trade_FindSelectedSlotIndex.
//   if (!dword_672228) return -1;            // nothing clicked this frame
//   scan 16 slots (stride 7 dwords): return the index whose slot id (+0) equals
//   the clicked widget id (dword_62D22C); -1 if none / slot empty (-1).
// `clickActive` mirrors dword_672228; `clickedId` mirrors dword_62D22C. `slots`
// is the slot-id array (one id per slot, the +0 dword of each 7-dword entry).
// ===========================================================================
int TradeFindSelectedSlotIndex(bool clickActive, i32 clickedId,
                               const std::vector<i32>& slots);

// ===========================================================================
// Command hook (lockstep). The buy dialog commits its balance move via
// VIBE_Command_EnqueueCmd15(payer, recipient, amount, currency). We record one
// command so a test can assert the exact leg emitted.
// ===========================================================================
struct TradeMoneyCommand {
    i32 payer = 0;
    i32 recipient = 0;
    i32 amount = 0;
    int currency = 0;
};
using TradeMoneyHook = void (*)(const TradeMoneyCommand& cmd, void* ctx);
void TradeSetMoneyHook(TradeMoneyHook hook, void* ctx);
void TradeEmitMoney(const TradeMoneyCommand& cmd);

// ===========================================================================
// gilde.exe 0x519b14 — VIBE_WineCellar_ShowBuyDialog (the player BUY rule core).
//
// The dialog reads the player's held cash `playerCash` (SumChildMoney of the
// player account) and, on the 1210 "buy" action, computes
//   cost = MultiplyByRate(itemValue, city);          // VIBE_Money_MultiplyByRate
//   if (playerCash >= cost)                            // can afford
//     EnqueueCmd15(playerAccount, sellerAccount, playerCash - cost, city);
//   else                                               // overdraft path
//     EnqueueCmd15(sellerAccount, playerAccount, cost - playerCash, city);
// The two legs encode the SAME net effect (player pays `cost`); the original
// always passes the post-trade *delta* of the player's cash so the lockstep
// reconciles to the new balance. We expose the deterministic numbers (cost,
// afford) and, when `commit`, emit the chosen leg.
// ===========================================================================
struct BuyInput {
    i32 itemValue = 0;     // VIBE_Object_GetDataPtr(slot): the item's raw value
    u8  city = 0;          // byte_6477A1 (the rate-lookup city/currency context)
    i32 playerCash = 0;    // SumChildMoney(playerAccount)
    i32 playerAccount = 0; // dword_12CE914[..]
    i32 sellerAccount = 0; // dword_63174C+2 (the stall/cellar account)
};
struct BuyResult {
    i32  cost = 0;     // MultiplyByRate(itemValue, city)
    bool afford = false;
};
BuyResult WineCellarBuy(const BuyInput& in, bool commit);

// ===========================================================================
// gilde.exe 0x50c5d4 — VIBE_Trade_RegisterEinkaufContact.
//
// The buy-contact router. Only fires in a FOREIGN city (homeCity != currentCity).
// It probes the panel object for one of five "Einkauf" contact names, in order,
// and (if the matched contact is the clicked one) opens the trade panel in mode 1
// (the "buy" / import variant). This recovers the contact->good-class mapping and
// the foreign-city gate. We model name presence as a bitmask over the five names
// and the click via `clickedIsContact`.
// ===========================================================================
enum class EinkaufContact {
    None = -1,
    General = 0,  // contact_EINKAUF
    Metal = 1,    // contact_EINKAUF_METALL
    Scripts = 2,  // contact_EINKAUF_SKRIPTE
    Wood = 3,     // contact_EINKAUF_HOLZ
    Stone = 4,    // contact_EINKAUF_STEIN
};
struct EinkaufContext {
    u16  homeCity = 0;     // word_63CC5C
    u16  currentCity = 0;  // *(obj+39)
    bool present[5] = {false, false, false, false, false}; // which names exist
    bool clickedIsContact = false; // the matched contact is the clicked widget
};
struct EinkaufResult {
    EinkaufContact matched = EinkaufContact::None; // first present contact
    bool openPanel = false;  // the trade panel (mode 1) was opened
};
// `openHook` (optional) is invoked when the panel would open (the original calls
// VIBE_TradeTransport_OpenPanelMode1_Thunk). nullptr -> just report.
using EinkaufOpenHook = void (*)(void* ctx);
EinkaufResult TradeRegisterEinkaufContact(const EinkaufContext& in,
                                          EinkaufOpenHook openHook, void* ctx);

} // namespace guild::world
