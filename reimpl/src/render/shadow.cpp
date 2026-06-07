#include "render/shadow.h"

// =============================================================================
// guild::render shadow setup — implementation. The geometry/raster routines are
// deferred (see shadow.h); this file translates the caster/surface-slot
// bookkeeping verbatim from the listed gilde.exe functions.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x5f4710 — VIBE_Shadow_FreeCasterEntry. The original unlinks the
// caster's back-pointers and releases the backing texture record; the entry id
// and its surface link are cleared. We model the state transition (zero entry).
// ---------------------------------------------------------------------------
void FreeCasterEntry(ShadowCaster& c) {
    if (c.entry) {
        // *(v1) = 0 ; v1[2] = 0 (surface link) ; release texture.
        c.entry = 0;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f474c — VIBE_Shadow_ClearAllCasters.
//   if ((+529 & 4) && +492) for each of 4 slots: if entry, free + clear state.
// ---------------------------------------------------------------------------
void ClearAllCasters(ShadowCaster table[kCasterSlots], bool enabled) {
    if (!enabled)
        return;
    for (int i = 0; i < kCasterSlots; ++i) {
        if (table[i].entry) {
            FreeCasterEntry(table[i]);
            table[i].lightId = 0;  // +1784
            table[i].stateA  = 0;  // +1804
            table[i].stateB  = 0;  // +1808
            table[i].stateC  = 0;  // +1812
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f47dc — VIBE_Shadow_RemoveCasterByLight.
//   if ((+529 & 4) && +492) for each of 4 slots: if entry && lightId == key,
//     free + clear. Always returns 1.
// ---------------------------------------------------------------------------
int RemoveCasterByLight(ShadowCaster table[kCasterSlots], bool enabled,
                        u32 lightId) {
    if (enabled) {
        for (int i = 0; i < kCasterSlots; ++i) {
            if (table[i].entry && table[i].lightId == lightId) {
                FreeCasterEntry(table[i]);
                table[i].lightId = 0;
                table[i].stateA  = 0;
                table[i].stateB  = 0;
                table[i].stateC  = 0;
            }
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Bind a slot (gilde.exe 0x5f1ddc VIBE_Shadow_BindCacheSurface, modelled): the
// slot takes ownership for (id,type) at the requested size and records the
// surface handle. Returns the slot index.
// ---------------------------------------------------------------------------
static int BindSlot(ShadowSurfaceCache& c, int idx, u32 id, u8 type, u32 need,
                    u32 handle) {
    ShadowSurfaceSlot& s = c.slots[(size_t)idx];
    s.bound = handle;       // a1[2] = a5
    // if (need == s.size && type == s.type) keep; else re-create at `need`.
    if (!(need == s.size && type == s.type)) {
        s.type = type;      // *((_BYTE*)a1+16) = a4
        s.size = need;      // a1[3] ... here size key
    }
    s.id = id;              // slot owned by this caster/light
    return idx;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5f1e7c — VIBE_Shadow_AcquireCacheSlot. Faithful three-phase scan:
//   Phase 1: walk slots; track best reusable occupied slot `v6` matching
//            (id,type) with need > slot.size (largest such); break at the first
//            FREE slot (id==0) -> v13.
//   - if a free slot was found, bind it.
//   - else if v6 and (need - v6.size) > minSpread, bind v6.
//   Phase 2: re-scan for the best matching occupied slot under `need`; if found
//            and (need - that size) > 1, bind it; else fail (-1).
// (a4=id@ebx, a2=type@dl, a3=need@ecx in the original; the unsigned compares
// are preserved.)
// ---------------------------------------------------------------------------
int ShadowSurfaceCache::AcquireCacheSlot(u32 id, u8 type, u32 need, u32 handle) {
    int v6 = -1;        // best reusable occupied slot
    int v13 = -1;       // first free slot
    u32 a3 = need;      // shrinking threshold (== slot.size of tracked v6)

    // Phase 1
    for (size_t i = 0; i < slots.size(); ++i) {
        ShadowSurfaceSlot& s = slots[i];
        if (id == s.id && s.type == type && a3 > s.size) {
            v6 = (int)i;
            a3 = s.size;            // a3 = *(v5+8)
        }
        if (s.id == 0) {            // free slot (*(v5+12)==0)
            v13 = (int)i;
            break;
        }
    }

    if (v13 >= 0)
        return BindSlot(*this, v13, id, type, need, handle);
    if (v6 >= 0 && (need - a3) > minSpread)
        return BindSlot(*this, v6, id, type, need, handle);

    // Phase 2
    v6 = -1;
    u32 v8 = need;
    for (size_t i = 0; i < slots.size(); ++i) {
        ShadowSurfaceSlot& s = slots[i];
        if (s.id != 0 && s.type == type && v8 > s.size) {
            v6 = (int)i;
            v8 = s.size;
        }
    }
    if (v6 >= 0 && (need - v8) > 1)
        return BindSlot(*this, v6, id, type, need, handle);
    return -1;
}

} // namespace guild::render
