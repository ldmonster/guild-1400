#include "sim/npcaction5.h"

#include "sim/gametime.h"
#include "sim/npcaction.h"   // NpcClock(), GetNpcLeafHooks()

#include <cstdint>

namespace guild::sim {

static inline void StampClock(GameTime& dst) { dst = NpcClock(); }

// The +180 region holds a 14-byte GameTime image in ResolveTargetAndReset (the
// original stores qword@+180, dword@+188, word@+192). View it as a GameTime.
static inline GameTime& He_DeadlineAt180(HeRecord* h) {
    return *reinterpret_cast<GameTime*>(HeBytes(h) + 180);
}

// ===========================================================================
// NpcAction5 entity-resolve hook plumbing.
// ===========================================================================
static const NpcAction5Hooks kInertHooks{};
static const NpcAction5Hooks* g_hooks5 = &kInertHooks;
void SetNpcAction5Hooks(const NpcAction5Hooks* hooks) {
    g_hooks5 = hooks ? hooks : &kInertHooks;
}
const NpcAction5Hooks& GetNpcAction5Hooks() { return *g_hooks5; }

// gilde.exe 0x4db248 — VIBE_NpcEvent_CountdownTickEntity.
i32 NpcAction5_CountdownTickEntity(HeRecord* h) {
    const auto& hooks = GetNpcLeafHooks();
    // if (state < 0 || counter <= 0) -> free
    if (He_State(h) < 0 || He_Counter172(h) <= 0) {
        return hooks.freeHandlerEntry ? hooks.freeHandlerEntry(h)
                                      : static_cast<i32>(reinterpret_cast<intptr_t>(h));
    }
    i32 result = static_cast<i32>(reinterpret_cast<intptr_t>(h));
    if ((He_Flags(h) & 4) == 0) {          // flag 0x04 not set
        --He_Counter172(h);                // +172
        StampClock(He_ApptTime(h));        // +82 <- clock
        GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);  // +24h
        result = hooks.queueRequestEntity29 ? hooks.queueRequestEntity29(0, h) : 0;
        He_ReqHandle(h) = result;          // +132
    }
    return result;
}

// gilde.exe 0x4d877c — VIBE_NpcEvent_ResolveTargetAndReset.
i32 NpcAction5_ResolveTargetAndReset(HeRecord* h) {
    const auto& hooks = GetNpcLeafHooks();
    const auto& hooks5 = GetNpcAction5Hooks();

    auto freeEntry = [&]() -> i32 {
        return hooks.freeHandlerEntry ? hooks.freeHandlerEntry(h)
                                      : static_cast<i32>(reinterpret_cast<intptr_t>(h));
    };

    // ResolveEntityById(+172): must resolve and its +97 field must be nonzero.
    i32 filterId = He_Counter172(h);       // +172
    i32 field97 = hooks5.resolveEntityField97 ? hooks5.resolveEntityField97(filterId) : 0;
    if (field97 == 0)
        return freeEntry();

    // Inventory item id = HIWORD(*(h+192)); must exist with a nonzero count.
    u32 packed192 = static_cast<u32>(*reinterpret_cast<i32*>(HeBytes(h) + 192));
    int itemId = static_cast<int>((packed192 >> 16) & 0xFFFF);
    i32 count = 0;
    int found = hooks.findInventorySlot ? hooks.findInventorySlot(itemId, &count) : 0;
    if (!found || count == 0)
        return freeEntry();

    // Copy clock into the +180 GameTime slot, advance by `count` minutes.
    He_DeadlineAt180(h) = NpcClock();      // +180..+193 <- clock
    i32 result = GameTimeAdvance(&He_DeadlineAt180(h), 0, 0, count);
    He_TargetObjId(h) = 0;                 // +176 := 0
    He_State(h) = 0;                       // +112 := 0
    return result;
}

// ===========================================================================
// Registration.
// ===========================================================================
namespace {
struct Binding { int address; i32 (*fn)(HeRecord*); };
const Binding kBindings[] = {
    { 0x4db248, &NpcAction5_CountdownTickEntity },
    { 0x4d877c, &NpcAction5_ResolveTargetAndReset },
};
} // namespace

int RegisterNpcActions5() {
    return static_cast<int>(sizeof(kBindings) / sizeof(kBindings[0]));
}

i32 (*NpcAction5_TableEntry(int address))(HeRecord*) {
    for (const auto& b : kBindings)
        if (b.address == address)
            return b.fn;
    return nullptr;
}

} // namespace guild::sim
