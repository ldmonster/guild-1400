#pragma once
// ===========================================================================
// market_stall.{h,cpp} — market stall STOCK management + the sorted trade-item
// list core (gilde.exe). MODULE: the MarketStall_* bodies and the trade-entry
// stock apply engine.
// ===========================================================================
//
// Recovered byte-for-byte:
//
//   * VIBE_Command_ExUpsertTradeEntry 0x49d2e8 — the trade-entry (market-stall
//     stock) APPLY engine. Finds or creates the per-building trade-entry object
//     (scene type 202), copies the order fields, and ACCUMULATES the stock byte
//     at entry+55, CLAMPED to 100 (0x64). This is the stall's stock book-keeping.
//   * VIBE_MarketStall_RouteContactByType 0x519918 — maps a market contact object
//     name to one of six stall good-category ids (146..151) + the two
//     non-stall board/tribune contacts (pamphlet / poem). The stall stock/price
//     panel is keyed by this category id.
//   * VIBE_Trade_BuildSortedItemList 0x51b26c — the SORT core of the trade item
//     list: a stable insertion/bubble sort of the slot records by their
//     localized currency name (home currency sorts first as "AAAA…", the empty
//     currency last as "ZZZZ…"). The widget-layout pass that follows the sort is
//     GUI and is deferred.
//
// The scene-tree find/add + field copy that ExUpsertTradeEntry performs on the
// live object graph is routed through a settable command hook; the deterministic
// stock arithmetic (accumulate + clamp) is modeled directly and golden-testable.
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Market stall category ids (gilde.exe — the second arg OpenStall receives per
// contact name). Six stall types + the two non-stall board contacts.
// ===========================================================================
enum class StallType {
    None = 0,
    TischlerSchmiede = 146, // ob_MARKTSTAND_TISCHLER_SCHMIEDE  (carpenter/smith)
    SteinmetzWirt = 147,    // ob_MARKTSTAND_STEINMETZ_WIRT     (mason/innkeeper)
    ParfumKraeuter = 148,   // ob_MARKTSTAND_PARFUM_KRAEUTER    (perfume/herbs)
    Rohstoffe = 149,        // ob_MARKTSTAND_ROHSTOFFE          (raw materials)
    Importeur = 150,        // ob_MARKTSTAND_IMPORTEUR          (importer)
    Kirche = 151,           // ob_MARKTSTAND_KIRCHE             (church stall)
    PamphletBoard,          // ob_SCHWARZES_BRETT / contact_..  (notice board)
    Tribune,                // ob_TRIBUENE                      (poem tribune)
};

// gilde.exe 0x519918 — VIBE_MarketStall_RouteContactByType. Resolve a clicked
// contact object name to the stall/board it opens. `contactName` is the clicked
// contact's name (dword_631720). Returns StallType::None when no name is clicked
// or the name is unknown. The probe order matches the original's if-ladder.
StallType MarketStallRouteContact(const std::string& contactName);

// ===========================================================================
// Trade-entry (stall stock) record — the scene object type 202 the upsert engine
// writes. Only the inventory-relevant fields the engine touches are modeled.
// ===========================================================================
struct TradeEntry {
    i32 id = 0;         // entry+1   (the scene object id; -1 == empty)
    i32 fieldA = 0;     // entry+28  <- order+24
    i32 fieldB = 0;     // entry+32  <- order+28
    i32 fieldC = 0;     // entry+36  <- order+32
    i16 fieldD = 0;     // entry+40  <- order+36 (word)
    i32 key = 0;        // entry+84  <- order+38 (the per-entry match key)
    i32 fieldE = 0;     // entry+100 <- order+46
    u8  flag = 0;       // entry+54  <- order+50
    u8  stock = 0;      // entry+55  accumulated, clamped to 100
};

// The order packet the upsert reads (the command payload `a1`).
struct TradeOrder {
    i32 dst = 0;        // a1+38  destination building id (must resolve)
    i32 src = 0;        // a1+20  source building id (must resolve; owns the entry)
    i32 fieldA = 0;     // a1+24
    i32 fieldB = 0;     // a1+28
    i32 fieldC = 0;     // a1+32
    i16 fieldD = 0;     // a1+36
    i32 key = 0;        // a1+38  match key (same dword as dst here)
    i32 fieldE = 0;     // a1+46
    u8  flag = 0;       // a1+50
    u8  stockDelta = 0; // a1+51  amount to add to entry+55
};

// gilde.exe 0x49d2e8 — VIBE_Command_ExUpsertTradeEntry (stock apply).
//   resolve dst + src buildings (both must exist), find the type-202 entry whose
//   key (+84) matches the order, else create it; copy the order fields and
//   accumulate stock: entry.stock = min(entry.stock + order.stockDelta, 100).
// `entries` is the source building's trade-entry list (modeled directly).
// Returns 0 on success (the entry was upserted), 1 on a resolution failure
// (matching the original's early `return 1`). On success and `applyState`
// non-null, *applyState is set to 1 (mirrors the original's a2 status byte).
int MarketStallUpsertTradeEntry(const TradeOrder& order, bool dstResolved,
                                bool srcResolved, std::vector<TradeEntry>& entries,
                                int* applyState);

// ===========================================================================
// Trade item list sort (gilde.exe 0x51b26c, sort core). Each slot carries a
// currency id at slot+12 (the give side) / slot+3 (a parallel column the original
// also reads). The sort key is the localized currency NAME:
//   * currency == homeCurrency  -> "AAAAAAAAA" (sorts first)
//   * currency == 0 (none)      -> "ZZZZZZZZZ" (sorts last)
//   * otherwise                 -> the currency's localized name string
// The original reads the currency id at slot+12 for BOTH compared slots and, for
// each ordered pair (i, j) with j>i, swaps the two whole 28-byte records when
//   StrCmp( name(slot[j]+12), name(slot[i]+12) ) == 1
// (i.e. name(j) sorts strictly AFTER name(i)). With "AAAA…" for the home currency
// and "ZZZZ…" for the none-currency, this orders the home currency to the FRONT
// and empties to the BACK. We model one slot as a SortSlot keyed on its +12
// currency id and reproduce that exact swap rule; the widget layout that follows
// is deferred.
// ===========================================================================
struct SortSlot {
    i32 id = -1;            // slot+0  (-1 == empty)
    i32 secondaryId = 0;    // slot+4
    i32 currency = 0;       // slot+12 (the sort-key currency id)
    int payload = 0;        // opaque rider preserved across swaps
};

// The localized-name resolver the sort calls: currency id -> name. Tests inject a
// map; home currency and 0 are handled by the sort itself (AAAA / ZZZZ).
using CurrencyNameFn = std::string (*)(i32 currencyId, void* ctx);

// Count of leading non-empty, non-(-1) slots the original determines before
// sorting (the v34 prefix length). Exposed for parity testing.
int TradeSortableCount(const std::vector<SortSlot>& slots);

// gilde.exe 0x51b26c sort core. Sorts the first `TradeSortableCount` slots by the
// currency-name key. `nameFn`/`ctx` resolve a currency id to its localized name
// (used for ids other than home / 0). `homeCurrency` is the player's home
// currency id (dword_649A88 column). Stable; mirrors the StrCmp==1 swap rule.
void TradeBuildSortedItemList(std::vector<SortSlot>& slots, i32 homeCurrency,
                              CurrencyNameFn nameFn, void* ctx);

} // namespace guild::world
