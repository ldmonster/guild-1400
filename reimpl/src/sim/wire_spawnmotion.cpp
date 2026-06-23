// See wire_spawnmotion.h. Binds the two reconstructed-backed ILifecycleHooks
// leaves; all other lifecycle fields keep their inert defaults (seed-from-
// defaults: the installed table subclasses the module's inert ILifecycleHooks
// and overrides ONLY the two bindable virtuals).
#include "sim/wire_spawnmotion.h"

#include "sim/building_lifecycle.h"  // ILifecycleHooks / SetLifecycleHooks / BuildingPersonAt
#include "sim/building3.h"           // Building3_ReleaseOccupantHoldings (0x589468)
#include "sim/object.h"              // GameObjectFreeChildList            (0x585aa4)

#include "guild/common/types.h"

#include <cstring>

namespace guild::sim {

namespace {

// In the original, the building record is &word_12CE910[268*slot] (a 536-byte
// Person slot). Our BuildingPersonAt(slot) resolves that same record; the child-
// list head field the original frees is at v2+188 (__int16* arithmetic) == byte
// +376 inside the record.
constexpr int kChildListHeadOff = 376;  // word_12CE910[268*slot] + 188 (words) -> +376 bytes

// Seed-from-defaults: inherit every inert ILifecycleHooks virtual, override only
// the two leaves that have a faithful reconstruction in a sibling cluster.
struct RealLifecycleHooks final : ILifecycleHooks {
    // VIBE_Building_ReleaseOccupantHoldings(rec) @0x589468.
    // The original's `result@eax` IS the record pointer; actorWord == *(u16*)rec
    // (the slot's +0 marker word). The reconstruction (building3.cpp) reproduces
    // the 0xFFFF guard + ChangePlayerAction/CancelEntityActions/
    // EventCancelMatchingActors/OfficeReleaseCharacterHoldings chain 1:1 (its own
    // sub-leaves route through Building3Hooks, inert by default).
    void ReleaseOccupantHoldings(int slot) override {
        BuildingPersonRec* rec = BuildingPersonAt(slot);
        if (!rec)
            return;
        u16 actorWord;
        std::memcpy(&actorWord, reinterpret_cast<const u8*>(rec), sizeof(actorWord));
        Building3_ReleaseOccupantHoldings(reinterpret_cast<const u8*>(rec), actorWord);
    }

    // VIBE_GameObject_FreeChildList(rec + 188_words == +376 bytes) @0x585aa4.
    // The reconstruction (object.cpp) walks the sibling/child chain from the head
    // field 1:1 (returns -1 for an empty head — the faithful no-op when the slot
    // carries no live scene children).
    void FreeChildList(int slot) override {
        BuildingPersonRec* rec = BuildingPersonAt(slot);
        if (!rec)
            return;
        i32* headField =
            reinterpret_cast<i32*>(reinterpret_cast<u8*>(rec) + kChildListHeadOff);
        GameObjectFreeChildList(headField);
    }
};

// Process-lifetime installed table (the global hook ptr references this).
RealLifecycleHooks g_realLifecycle;

}  // namespace

void InstallRealSpawnMotionWiring() {
    SetLifecycleHooks(&g_realLifecycle);
}

}  // namespace guild::sim
