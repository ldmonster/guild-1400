#include "sim/building6.h"

#include <cstdint>
#include <cstring>

#include "sim/building_type.h"   // REAL sibling: Building_MapKindToCategory
#include "sim/building3.h"       // REAL sibling: Building3_CheckTimeWindowOpen

// VIBE_Building_CheckEntryAllowed (gilde.exe 0x51dcd4). See building6.h.
//
// Globals recovered:
//   word_63CC5C   — active player / selected-building person index.
//   word_63C740   — global flag word; bit 7 (0x80) == "free entry everywhere".
//   byte_12CEAC1  — active player record byte at +0x1B1 (VIP/owner-of-kind flag).
//   qword_13CE852 — the game clock; WORD2 (hour) is only used to pick the closed-
//                   message wording, which is a UI side effect (hooked).
//   Message ids:  7216/7217 (closed, foreign owner), 7219/7220 (closed, public),
//                 7218 (the "open HH:MM-HH:MM" suffix).

namespace guild::sim {

namespace {
Building6Hooks  g_defaultHooks;
Building6Hooks* g_hooks = &g_defaultHooks;

int  g_activePlayer        = 0;       // word_63CC5C
bool g_freeEntryEverywhere = false;   // word_63C740 & 0x80
}  // namespace

void SetBuilding6Hooks(Building6Hooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
Building6Hooks* Building6HooksGet() { return g_hooks; }

void SetEntryGateContext(int activePlayerIndex, bool freeEntryEverywhere) {
    g_activePlayer = activePlayerIndex;
    g_freeEntryEverywhere = freeEntryEverywhere;
}

// gilde.exe 0x51dcd4 — VIBE_Building_CheckEntryAllowed
int Building_CheckEntryAllowed(const std::uint8_t* building) {
    if (!building) return 0;

    unsigned activePlayer = static_cast<std::uint16_t>(g_activePlayer);

    // v2 = type record base (589 * *a1 + dword_13CE294); the gate reads its KIND.
    const std::uint8_t* typeDef = g_hooks->TypeDef(
        static_cast<unsigned>(static_cast<std::int8_t>(building[0])));
    int kind = typeDef ? typeDef[0] : 0;        // *v2

    // v19 = MapTypeToCategory(*a1). MapTypeToCategory reads the SAME kind byte and
    // delegates to the (real) kind->category classifier.
    u8 category = Building_MapKindToCategory(static_cast<u8>(kind));

    // v4 = ComputeSelectionFlags(activePlayer, a1, 0, 0).
    std::int16_t selFlags = g_hooks->ComputeSelectionFlags(activePlayer, building);

    // 1. +91 & 4 -> forbidden.
    if ((building[91] & 4) != 0)
        return 0;

    // 2. Any of: free-entry-everywhere, VIP byte for kinds {6,4}, force-allow
    //    selection bit (0x200), or the active player owns the building (+39).
    std::uint8_t vip = g_hooks->ActivePlayerVipByte(activePlayer);
    std::uint16_t owner;
    std::memcpy(&owner, building + 39, sizeof owner);
    if (g_freeEntryEverywhere
        || (vip && (kind == 6 || kind == 4))
        || (selFlags & 0x200) != 0
        || owner == static_cast<std::uint16_t>(activePlayer)) {
        return 1;
    }

    // 3. Not selectable -> denied.
    if ((selFlags & 1) == 0)
        return 0;

    // 4. Open right now -> allowed. Delegates to the REAL sibling time-window
    //    check (gilde.exe 0x51dc04). The original passes the building pointer and
    //    the check resolves the type record's KIND byte internally; the sibling
    //    reconstruction takes that resolved KIND directly.
    int openHour = 0, closeHour = 0;
    if (Building3_CheckTimeWindowOpen(static_cast<std::uint8_t>(kind),
                                      &openHour, &closeHour))
        return 1;

    // 5. Closed: emit the appropriate "closed" notice (UI side effect; hooked).
    //    The original picks the message id from the current hour vs the open hour
    //    and the category; we recover the id selection 1:1 and hand it to the
    //    hook. The clock hour reused here is the same one the sibling time-window
    //    check consulted, so re-deriving "now >= openHour" is faithful.
    int nowHour = Building3GameHour();
    int msgId;
    if (nowHour >= openHour) {
        msgId = (category == 3 || category == 5) ? 7220 : 7217;
    } else {
        msgId = (category == 3 || category == 5) ? 7219 : 7216;
    }
    g_hooks->ShowClosedMessage(msgId, building, openHour, closeHour);
    return 0;
}

}  // namespace guild::sim
