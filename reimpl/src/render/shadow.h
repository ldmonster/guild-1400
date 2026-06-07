#pragma once
#include "guild/common/types.h"
#include <vector>

// =============================================================================
// guild::render — projected-shadow setup / caster bookkeeping. Faithful 1:1
// reconstruction of the self-contained list-management half of the gilde.exe
// shadow cluster (d3_engine.c shadow code):
//
//   0x5f1e7c  VIBE_Shadow_AcquireCacheSlot   (best-fit shadow-surface slot)
//   0x5f474c  VIBE_Shadow_ClearAllCasters    (free a node's 4 caster entries)
//   0x5f47dc  VIBE_Shadow_RemoveCasterByLight(drop the caster cast by a light)
//   0x5f4710  VIBE_Shadow_FreeCasterEntry    (release one caster entry)
//
// The geometry-heavy projection / rasterisation (ProjectGroundQuad,
// RenderMeshShadow, CastFromLight, RasterizeHeightField) are DEFERRED — they are
// large FPU/raster routines outside this module's setup scope (see report).
//
// PER-OBJECT CASTER TABLE — 4 entries, 128-byte stride, at object +492+1780:
//   [+0]    caster entry id (0 = empty slot)
//   [+4]    light id that cast this shadow (key for RemoveCasterByLight)
//   [+24]   cached state A   (orig +1804)
//   [+28]   cached state B   (orig +1808)
//   [+32]   cached state C   (orig +1812)
// ClearAllCasters / RemoveCasterByLight only run when the object's render flag
// (+529 bit2, 0x4) is set.
//
// SHADOW-SURFACE CACHE — 20-byte slots (count dword_64A7E0, base dword_64A7F8):
//   [+8]  size, [+12] caster/light id (0 = free), [+16] type flag.
// AcquireCacheSlot best-fits an existing slot for (id,type) preferring the
// smallest that still fits, else evicts.
// =============================================================================
namespace guild::render {

// One per-object shadow caster entry (the 128-byte record's lighting fields).
struct ShadowCaster {
    u32 entry = 0;     // +1780  caster id/ptr (0 = empty)
    u32 lightId = 0;   // +1784  casting light id
    u32 stateA = 0;    // +1804
    u32 stateB = 0;    // +1808
    u32 stateC = 0;    // +1812
};

// gilde.exe 0x5f4710 — VIBE_Shadow_FreeCasterEntry. Releases one caster (here:
// zeroes the entry id; the original frees the backing surface link).
void FreeCasterEntry(ShadowCaster& c);

// A node's caster table is exactly 4 slots (512 bytes / 128).
constexpr int kCasterSlots = 4;

// gilde.exe 0x5f474c — VIBE_Shadow_ClearAllCasters. Frees every occupied slot
// and clears its cached state. `enabled` mirrors the (+529 & 4) gate.
void ClearAllCasters(ShadowCaster table[kCasterSlots], bool enabled);

// gilde.exe 0x5f47dc — VIBE_Shadow_RemoveCasterByLight. Frees the slot whose
// lightId == `lightId` (and clears its cached state). Returns 1 (the original
// always returns 1). `enabled` mirrors the (+529 & 4) gate.
int RemoveCasterByLight(ShadowCaster table[kCasterSlots], bool enabled,
                        u32 lightId);

// ---------------------------------------------------------------------------
// Shadow-surface cache (20-byte slots). `id` 0 == free.
// ---------------------------------------------------------------------------
struct ShadowSurfaceSlot {
    u32 size = 0;   // +8   slot capacity
    u32 id   = 0;   // +12  caster/light id (0 = free)
    u8  type = 0;   // +16  type flag (a2)
    u32 bound = 0;  // bound surface handle (set by BindCacheSurface)
};

struct ShadowSurfaceCache {
    std::vector<ShadowSurfaceSlot> slots;  // dword_64A7F8 base
    u32 minSpread = 0;                      // dword_64A7E4 reuse threshold

    explicit ShadowSurfaceCache(int capacity) : slots((size_t)capacity) {}

    // gilde.exe 0x5f1e7c — VIBE_Shadow_AcquireCacheSlot. Find the best slot to
    // hold a shadow of capacity `need` for (id,type): first an *occupied* slot
    // matching (id,type) whose size is the smallest that still exceeds `need`
    // (reuse), then a free slot (id==0), else evict the matching-type slot whose
    // size is closest under `need`. Returns slot index or -1.
    // `handle` is the surface handle bound into the slot on success.
    int AcquireCacheSlot(u32 id, u8 type, u32 need, u32 handle);
};

} // namespace guild::render
