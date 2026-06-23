// =============================================================================
// Golden-vector + edge/degenerate tests for the shadow caster bookkeeping and
// the shadow-surface cache in src/render/shadow.{h,cpp}:
//   0x5f4710 VIBE_Shadow_FreeCasterEntry
//   0x5f474c VIBE_Shadow_ClearAllCasters
//   0x5f47dc VIBE_Shadow_RemoveCasterByLight
//   0x5f1e7c VIBE_Shadow_AcquireCacheSlot  (best-fit slot scan)
//
// Wave-10 HARDENING: these list-bookkeeping routines had NO direct coverage; this
// file pins the 4-slot caster cache (full / reuse / evict) and exercises the
// cache-slot scan's bounds (empty cache, single slot, all-occupied, evict path)
// under ASAN+UBSAN. No golden values change — this is new coverage.
// =============================================================================
#include "tests/framework/test.h"
#include "render/shadow.h"

#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// FreeCasterEntry (0x5f4710) — zeroes a non-empty entry id; leaves empty alone.
// ---------------------------------------------------------------------------
TEST(ShadowCache, FreeCasterEntry) {
    ShadowCaster c;
    c.entry = 7; c.lightId = 3; c.stateA = 1;
    FreeCasterEntry(c);
    CHECK_EQ(c.entry, 0u);
    // FreeCasterEntry only clears the entry id (the surface link); lightId/state
    // are cleared by the callers, not here.
    CHECK_EQ(c.lightId, 3u);

    // Already-empty entry: the `if (c.entry)` guard makes it a no-op.
    ShadowCaster e;  // entry == 0
    e.lightId = 9;
    FreeCasterEntry(e);
    CHECK_EQ(e.entry, 0u);
    CHECK_EQ(e.lightId, 9u);
}

// ---------------------------------------------------------------------------
// ClearAllCasters (0x5f474c) — the 4-slot caster cache FULL case: every occupied
// slot is freed + its cached state cleared; empty slots untouched; gate honored.
// ---------------------------------------------------------------------------
TEST(ShadowCache, ClearAllCastersFullTable) {
    ShadowCaster t[kCasterSlots];
    for (int i = 0; i < kCasterSlots; ++i) {
        t[i].entry = (u32)(i + 1);          // all occupied (cache full)
        t[i].lightId = (u32)(100 + i);
        t[i].stateA = 1; t[i].stateB = 2; t[i].stateC = 3;
    }
    ClearAllCasters(t, /*enabled=*/true);
    for (int i = 0; i < kCasterSlots; ++i) {
        CHECK_EQ(t[i].entry, 0u);
        CHECK_EQ(t[i].lightId, 0u);
        CHECK_EQ(t[i].stateA, 0u);
        CHECK_EQ(t[i].stateB, 0u);
        CHECK_EQ(t[i].stateC, 0u);
    }
}

TEST(ShadowCache, ClearAllCastersEmptyAndDisabled) {
    // Disabled gate: nothing touched.
    ShadowCaster t[kCasterSlots];
    t[0].entry = 5; t[0].lightId = 11; t[0].stateA = 9;
    ClearAllCasters(t, /*enabled=*/false);
    CHECK_EQ(t[0].entry, 5u);
    CHECK_EQ(t[0].lightId, 11u);

    // Enabled but all-empty table: no writes, no OOB.
    ShadowCaster e[kCasterSlots];   // all entry == 0
    e[2].lightId = 42;              // empty slot keeps stale lightId
    ClearAllCasters(e, /*enabled=*/true);
    CHECK_EQ(e[2].lightId, 42u);
    for (int i = 0; i < kCasterSlots; ++i) CHECK_EQ(e[i].entry, 0u);
}

// ---------------------------------------------------------------------------
// RemoveCasterByLight (0x5f47dc) — frees only the slot(s) matching the light id;
// always returns 1; honors the gate. Reuse/evict-by-light boundary.
// ---------------------------------------------------------------------------
TEST(ShadowCache, RemoveCasterByLight) {
    ShadowCaster t[kCasterSlots];
    for (int i = 0; i < kCasterSlots; ++i) {
        t[i].entry = (u32)(i + 1);
        t[i].lightId = (u32)(200 + i);
        t[i].stateA = 7;
    }
    // Remove the slot cast by light 202 (slot 2).
    CHECK_EQ(RemoveCasterByLight(t, /*enabled=*/true, 202u), 1);
    CHECK_EQ(t[2].entry, 0u);
    CHECK_EQ(t[2].lightId, 0u);
    CHECK_EQ(t[2].stateA, 0u);
    // Others survive.
    CHECK_EQ(t[0].entry, 1u);
    CHECK_EQ(t[3].entry, 4u);

    // No matching light: returns 1, nothing freed.
    CHECK_EQ(RemoveCasterByLight(t, true, 9999u), 1);
    CHECK_EQ(t[0].entry, 1u);

    // Disabled gate: returns 1, nothing touched even on a match.
    CHECK_EQ(RemoveCasterByLight(t, false, 200u), 1);
    CHECK_EQ(t[0].entry, 1u);
}

// ---------------------------------------------------------------------------
// AcquireCacheSlot (0x5f1e7c) — surface-cache slot scan.
// ---------------------------------------------------------------------------

// Empty cache: the first free slot (id==0) is bound (phase-1 break-on-free).
TEST(ShadowCache, AcquireEmptyBindsFirstFree) {
    ShadowSurfaceCache c(4);
    int idx = c.AcquireCacheSlot(/*id=*/5, /*type=*/1, /*need=*/16, /*handle=*/0xAB);
    CHECK_EQ(idx, 0);
    CHECK_EQ(c.slots[0].id, 5u);
    CHECK_EQ(c.slots[0].size, 16u);
    CHECK_EQ((int)c.slots[0].type, 1);
    CHECK_EQ(c.slots[0].bound, 0xABu);
}

// Zero-capacity cache: no slots to scan -> phase 1 + phase 2 both empty -> -1.
// Exercises the size()==0 loop bound (no OOB).
TEST(ShadowCache, AcquireZeroCapacityFails) {
    ShadowSurfaceCache c(0);
    CHECK_EQ(c.AcquireCacheSlot(1, 0, 8, 0x10), -1);
    CHECK_EQ((int)c.slots.size(), 0);
}

// Single-slot cache, all states: free -> bind; then full+matching -> reuse path.
TEST(ShadowCache, AcquireSingleSlot) {
    ShadowSurfaceCache c(1);
    // Free slot bound first.
    CHECK_EQ(c.AcquireCacheSlot(3, 2, 32, 0x99), 0);
    CHECK_EQ(c.slots[0].id, 3u);
    CHECK_EQ(c.slots[0].size, 32u);

    // Now occupied by (id=3,type=2,size=32). Request a bigger one for the same
    // (id,type): phase 1 has no free slot; v6 tracks it (need 64 > size 32) and
    // (need-a3)=32 > minSpread(0) -> reuse-bind it at the new size.
    int idx = c.AcquireCacheSlot(3, 2, 64, 0xBB);
    CHECK_EQ(idx, 0);
    CHECK_EQ(c.slots[0].size, 64u);
    CHECK_EQ(c.slots[0].bound, 0xBBu);
}

// FULL cache, no free slot, NO (id,type) match in phase 1, but phase 2 finds a
// same-type occupied slot whose size is under `need` by > 1 -> EVICT it.
TEST(ShadowCache, AcquireFullEvictsByType) {
    ShadowSurfaceCache c(3);
    // Occupy every slot; none shares (id,type)==(7,1) so phase-1 reuse misses.
    c.slots[0] = ShadowSurfaceSlot{ /*size*/ 8,  /*id*/ 1, /*type*/ 1, /*bound*/ 0 };
    c.slots[1] = ShadowSurfaceSlot{ /*size*/ 4,  /*id*/ 2, /*type*/ 1, /*bound*/ 0 };
    c.slots[2] = ShadowSurfaceSlot{ /*size*/ 30, /*id*/ 3, /*type*/ 2, /*bound*/ 0 };

    // need=32, type=1. Phase 1: no free, no (7,1) match -> falls to phase 2.
    // Phase 2 scans type==1 occupied slots for the smallest size: slot1 (size 4)
    // is the running minimum (4 < 8). (need - 4) = 28 > 1 -> evict slot 1.
    int idx = c.AcquireCacheSlot(/*id=*/7, /*type=*/1, /*need=*/32, /*handle=*/0xEE);
    CHECK_EQ(idx, 1);
    CHECK_EQ(c.slots[1].id, 7u);          // re-owned by the new caster
    CHECK_EQ(c.slots[1].size, 32u);
    CHECK_EQ(c.slots[1].bound, 0xEEu);
}

// FULL cache, no free, no match, and phase 2's best fit is within 1 of `need`
// (need - size <= 1) -> NO eviction, returns -1. The (id,type) all mismatch type.
TEST(ShadowCache, AcquireFullNoEvictReturnsMinusOne) {
    ShadowSurfaceCache c(2);
    // Both occupied, type 2 (request type 1 -> no phase-2 candidate at all).
    c.slots[0] = ShadowSurfaceSlot{ 10, 1, 2, 0 };
    c.slots[1] = ShadowSurfaceSlot{ 20, 2, 2, 0 };
    CHECK_EQ(c.AcquireCacheSlot(/*id=*/9, /*type=*/1, /*need=*/16, 0x1), -1);
    // Unchanged.
    CHECK_EQ(c.slots[0].id, 1u);
    CHECK_EQ(c.slots[1].id, 2u);
}

// minSpread threshold blocks the phase-1 reuse: (need - size) must EXCEED it.
TEST(ShadowCache, AcquireReuseBlockedByMinSpread) {
    ShadowSurfaceCache c(1);
    c.slots[0] = ShadowSurfaceSlot{ /*size*/ 30, /*id*/ 5, /*type*/ 1, /*bound*/ 0 };
    c.minSpread = 100;   // huge reuse threshold

    // need=40, matching (5,1): v6 tracks it, but (need-30)=10 is NOT > 100, so
    // phase 1 does not bind. Phase 2: same slot, (need-30)=10 > 1 -> evict-bind.
    int idx = c.AcquireCacheSlot(5, 1, 40, 0x7);
    CHECK_EQ(idx, 0);
    CHECK_EQ(c.slots[0].size, 40u);
}
