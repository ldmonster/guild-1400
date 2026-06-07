#include "world/market_stall.h"

namespace guild::world {

// gilde.exe 0x519918 — VIBE_MarketStall_RouteContactByType.
// The original registers eight status-text contacts, then matches the clicked
// name (dword_631720) against them in a fixed if-ladder, opening the matching
// stall (OpenStall with id 146..151) or the board/poem panel.
StallType MarketStallRouteContact(const std::string& name) {
    if (name.empty())
        return StallType::None;
    if (name == "ob_MARKTSTAND_TISCHLER_SCHMIEDE") return StallType::TischlerSchmiede;
    if (name == "ob_MARKTSTAND_STEINMETZ_WIRT")    return StallType::SteinmetzWirt;
    if (name == "ob_MARKTSTAND_PARFUM_KRAEUTER")   return StallType::ParfumKraeuter;
    if (name == "ob_MARKTSTAND_ROHSTOFFE")         return StallType::Rohstoffe;
    if (name == "ob_MARKTSTAND_IMPORTEUR")         return StallType::Importeur;
    if (name == "ob_MARKTSTAND_KIRCHE")            return StallType::Kirche;
    // The black-board has two accepted names (contact_ + bare); both -> board.
    if (name == "contact_ob_SCHWARZES_BRETT" || name == "ob_SCHWARZES_BRETT")
        return StallType::PamphletBoard;
    if (name == "ob_TRIBUENE")                     return StallType::Tribune;
    return StallType::None;
}

// gilde.exe 0x49d2e8 — VIBE_Command_ExUpsertTradeEntry (stock apply).
int MarketStallUpsertTradeEntry(const TradeOrder& order, bool dstResolved,
                                bool srcResolved, std::vector<TradeEntry>& entries,
                                int* applyState) {
    // The original pre-stamps the status byte to "pending" (2) before doing work.
    if (applyState)
        *applyState = 2;

    // if (!Building_FindById(dst)) return 1;
    if (!dstResolved)
        return 1;
    // v4 = Building_FindById(src); if (!v4) return 1;
    if (!srcResolved)
        return 1;

    // Find the type-202 entry whose match key (+84) equals the order's (+38).
    TradeEntry* e = nullptr;
    for (auto& cand : entries) {
        if (cand.key == order.key) {
            e = &cand;
            break;
        }
    }
    // Not found: AddObjekt(src, 202, ...) -> a fresh entry.
    if (!e) {
        TradeEntry fresh;
        fresh.id = static_cast<i32>(entries.size()) + 1; // mimic a new object id
        entries.push_back(fresh);
        e = &entries.back();
    }

    // Copy the order fields onto the entry (the verbatim field copy).
    e->fieldA = order.fieldA;  // +28 <- +24
    e->fieldB = order.fieldB;  // +32 <- +28
    e->fieldC = order.fieldC;  // +36 <- +32
    e->fieldD = order.fieldD;  // +40 <- +36
    e->key    = order.key;     // +84 <- +38
    e->fieldE = order.fieldE;  // +100 <- +46
    e->flag   = order.flag;    // +54 <- +50

    // Accumulate stock at +55, clamp to 100 (the original: v8 = *(+55)+delta;
    // if v8 <= 0x64 keep else 100).
    unsigned sum = static_cast<unsigned>(e->stock) + order.stockDelta; // u8 wrap as in orig
    u8 wrapped = static_cast<u8>(sum);
    e->stock = (wrapped <= 0x64u) ? wrapped : static_cast<u8>(100);

    if (applyState)
        *applyState = 1; // success
    return 0;
}

// gilde.exe 0x51b26c — the prefix-length determination (v34): scan from the last
// candidate (index 15) backwards while the slot is empty (id == -1 OR the +12
// column is 0); the first non-empty index +1 is the sortable count.
int TradeSortableCount(const std::vector<SortSlot>& slots) {
    int n = static_cast<int>(slots.size());
    if (n > 16)
        n = 16;
    for (int i = n - 1; i >= 0; --i) {
        const SortSlot& s = slots[i];
        if (s.id != -1 && s.currency != 0)
            return i + 1;
    }
    return 0;
}

namespace {
// Build the sort key string for one slot's currency, matching the original:
//   home currency -> "AAAAAAAAA"; 0 -> "ZZZZZZZZZ"; else the localized name.
std::string SortKey(i32 currency, i32 homeCurrency, CurrencyNameFn fn, void* ctx) {
    if (currency == homeCurrency)
        return "AAAAAAAAA";
    if (currency == 0)
        return "ZZZZZZZZZ";
    if (fn)
        return fn(currency, ctx);
    return "ZZZZZZZZZ";
}
} // namespace

// gilde.exe 0x51b26c sort core. Faithful to the original's nested loop and
// swap-when-greater rule (StrCmp(name(j), name(i)) == 1).
void TradeBuildSortedItemList(std::vector<SortSlot>& slots, i32 homeCurrency,
                              CurrencyNameFn nameFn, void* ctx) {
    int count = TradeSortableCount(slots);
    for (int i = 0; i < count; ++i) {
        std::string keyI = SortKey(slots[i].currency, homeCurrency, nameFn, ctx);
        for (int j = i + 1; j < count; ++j) {
            std::string keyJ = SortKey(slots[j].currency, homeCurrency, nameFn, ctx);
            // StrCmp(right=name(j), left=name(i)) == 1  <=>  name(j) > name(i).
            if (keyJ > keyI) {
                std::swap(slots[i], slots[j]);
                keyI = keyJ; // slots[i] now holds the (former) j record
            }
        }
    }
}

} // namespace guild::world
