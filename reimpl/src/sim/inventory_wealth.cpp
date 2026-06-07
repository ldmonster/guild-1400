#include "sim/inventory_wealth.h"

// Faithful 1:1 port of the currency / total-wealth aggregation from gilde.exe.
// The originals scan the global scene tree; here the person's currency stacks
// and owned-building worth terms are supplied as views so the summation is
// byte-faithful and testable. Provenance addresses on each function.

namespace guild::sim {

// Runtime goods table (currency proto per player) + active player.
static std::vector<i16> g_currencyProto;  // dword_13CD6F2[189*p] >> 16, per p
static u8 g_activePlayer = 0;             // byte_6477A1

void WealthSetCurrencyProtoTable(const std::vector<i16>& protoByPlayer) {
    g_currencyProto = protoByPlayer;
}
i16 WealthCurrencyProto(u8 player) {
    if (player < g_currencyProto.size())
        return g_currencyProto[player];
    return 0;
}
void WealthSetActivePlayer(u8 player) { g_activePlayer = player; }
u8 WealthActivePlayer() { return g_activePlayer; }

namespace {
// Find the currency child stack of `proto` in the person's container.
const StockChild* FindCurrency(const ContainerView& c, i16 proto) {
    for (const StockChild& ch : c.children)
        if (ch.type == proto)
            return &ch;
    return nullptr;
}
}  // namespace

// ===========================================================================
// VIBE_Person_GetCurrencyAmount  0x5915b8
//   v2 = QueryFind(*(person+376), 1, 0, currencyProto(player));
//   if (v2) return *(v2+14);  return <uninit>;     // missing -> 0
// ===========================================================================
int PersonGetCurrencyAmount(const ContainerView& container, u8 player) {
    i16 proto = WealthCurrencyProto(player);
    const StockChild* ch = FindCurrency(container, proto);
    return ch ? ch->level : 0;
}

// ===========================================================================
// VIBE_Person_SumCurrencyHeld  0x59152c
//   for (i = QueryFind(*(person+376), 1, 0, currencyProto(activePlayer)); i;
//        i = IterNext()) sum += *(i+14);
//   return sum;
// The iterator yields every sibling matching the proto filter; we sum the
// +14 amount (== StockChild.level) of each.
// ===========================================================================
int PersonSumCurrencyHeld(const ContainerView& container) {
    i16 proto = WealthCurrencyProto(g_activePlayer);
    int sum = 0;
    for (const StockChild& ch : container.children)
        if (ch.type == proto)
            sum += ch.level;
    return sum;
}

// ===========================================================================
// VIBE_Person_ComputeTotalWealth  0x591f7c
//   if (personIndex >= 0x300) return -1;
//   if (person.marker == -1)  return -1;
//   v4 = SumCurrencyHeld(person);
//   for (each owned building) v4 += roomWorth + storageWorth;
//   return v4;
// ===========================================================================
int PersonComputeTotalWealth(
    int personIndex, bool freeSlot, const ContainerView& currency,
    const std::vector<OwnedBuildingWorth>& ownedBuildings) {
    if (static_cast<unsigned>(personIndex) >= 0x300u)
        return -1;
    if (freeSlot)
        return -1;
    int v4 = PersonSumCurrencyHeld(currency);
    for (const OwnedBuildingWorth& b : ownedBuildings)
        v4 += b.roomWorth + b.storageWorth;
    return v4;
}

}  // namespace guild::sim
