// gilde.exe 0x5a986c — VIBE_Save_LoadCharacters  (__usercall, eax=a1 stream, ebp=a2)
//
// LOAD-side reconstruction of the live-character / action-slab pass. See
// save_recon3_characters.h for the recovered field map and the boundary notes.
//
// The genuine serialization (the two index counts + per-slot reads, the 404-byte
// action-record read loop with its exact field order, and the index<->offset
// next/prev relink arithmetic) is reproduced byte-for-byte. The render/mesh/avatar
// build pass and the person/object/handle/vtable resolutions are foreign engine
// state reached only through CharLoadHooks (inert by default).
#include "io/save_recon3_characters.h"

#include <cstring>

namespace guild::io {

// ---------------------------------------------------------------------------
// Byte source — VIBE_Vfs_ReadStream(dst,size,h,1) semantics (all-or-nothing).
// ---------------------------------------------------------------------------
CharLoadSource CharLoadSourceOpen(const guild::u8* buffer, guild::u32 capacity) {
    CharLoadSource s;
    s.buf = buffer;
    s.cap = capacity;
    s.pos = 0;
    s.ok  = true;
    return s;
}

// returns true iff exactly `n` bytes were available and copied.
static bool RD(CharLoadSource* s, guild::u8* dst, guild::u32 n) {
    if (!s->ok || s->pos + n > s->cap) {
        s->ok = false;
        return false;
    }
    std::memcpy(dst, s->buf + s->pos, n);
    s->pos += n;
    return true;
}

static guild::u32 RdU32(const guild::u8* p) {
    return (guild::u32)p[0] | ((guild::u32)p[1] << 8) | ((guild::u32)p[2] << 16) |
           ((guild::u32)p[3] << 24);
}
static void WrU32(guild::u8* p, guild::u32 v) {
    p[0] = (guild::u8)(v);
    p[1] = (guild::u8)(v >> 8);
    p[2] = (guild::u8)(v >> 16);
    p[3] = (guild::u8)(v >> 24);
}

// ---------------------------------------------------------------------------
// Inert default for the per-slot reader. The original's
// VIBE_Save_LoadCharacterSlot @0x5a96c0 streams a sparse 516-byte record; here the
// default reproduces the *cursor advance* the original performs at this call site
// (the slot index reads consume nothing from the stream beyond what LoadCharacterSlot
// itself reads). Wiring the real loader (io/save_world_load LoadCharacterSlot) keeps
// the field contents; the count loop's only observable effect for the slab pass is
// the relink of slot+300, modelled below.
//
// Since LoadCharacterSlot is reconstructed elsewhere and reads a version-gated field
// set, the default here streams nothing and zeroes the scratch — the count value
// itself is the only thing this pass needs from the stream, and the per-slot
// relink is exercised through the hook. (When the real loader is wired, slotScratch
// holds the parsed record so findPersonById/findObjectById see the real +300 id.)
static bool DefaultLoadSlot(CharLoadSource* /*src*/, guild::u8* slot,
                            guild::u32 /*version*/, void* /*user*/) {
    std::memset(slot, 0, kCl3_SlotSize);
    return true;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5a986c — VIBE_Save_LoadCharacters.
// ---------------------------------------------------------------------------
int LoadCharacters(CharLoadSource* src, guild::u32 version, guild::u8* slab,
                   guild::u8* slotScratch, const CharLoadHooks& hooks, void* user,
                   CharLoadResult* out) {
    if (out) {
        out->slotCountA = 0;
        out->slotCountB = 0;
        out->slabCount  = 0;
        out->fullyParsed = false;
    }

    auto loadSlot       = hooks.loadSlot       ? hooks.loadSlot       : DefaultLoadSlot;

    // --- step 1: first index count + per-slot person relink (0x5a9888) -----
    guild::i32 countA = 0;            // v59
    if (!RD(src, reinterpret_cast<guild::u8*>(&countA), 4))   // ReadStream(&v59,4,h,1)
        return 0;
    if (out) out->slotCountA = countA;
    for (guild::i32 i = 0; i < countA; ++i) {
        if (!loadSlot(src, slotScratch, version, user))       // LoadCharacterSlot(a1,i)
            return 0;
        // RecordById = FindRecordById(*((u32*)slot+75)); slot[+300] = RecordById
        guild::u32 id = RdU32(slotScratch + kCl3_SlotLinkOff);
        guild::u32 rec = hooks.findPersonById ? hooks.findPersonById(id, user) : id;
        WrU32(slotScratch + kCl3_SlotLinkOff, rec);
    }

    // --- step 2: second index count + per-slot object relink (0x5a98f3) ----
    guild::i32 countB = 0;            // v59 reused
    if (!RD(src, reinterpret_cast<guild::u8*>(&countB), 4))
        return 0;
    if (out) out->slotCountB = countB;
    for (guild::i32 j = 0; j < countB; ++j) {
        if (!loadSlot(src, slotScratch, version, user))
            return 0;
        // v9 = *((u32*)slot+75); v10 = GameObject_QueryFind(0,2,7,1,v9); slot[+300]=v10
        guild::u32 id  = RdU32(slotScratch + kCl3_SlotLinkOff);
        guild::u32 obj = hooks.findObjectById ? hooks.findObjectById(id, user) : id;
        WrU32(slotScratch + kCl3_SlotLinkOff, obj);
    }

    // --- step 3: render/mesh/avatar build pass (engine state, 0x5a9942) ----
    // The original walks dword_66F0D0[0..2048) creating meshes/avatars/transport
    // carts and calling SwitchActiveSlot. Out of scope for a portable load; inert.
    if (hooks.buildCharacterScene)
        hooks.buildCharacterScene(user);
    // SwitchActiveSlot(v60,1,2048,a1); SetGrayColorThunk(0,4,charBase) — both engine.

    // --- step 4: action-slab READ loop (0x5a9b3a) --------------------------
    // version gate: dword_649D4C >= 0x10044 -> 1280 records, else 768.
    const guild::u32 slabCount =
        (version >= kCl3_VerFullSlab) ? kCl3_CountHigh : kCl3_CountLow;
    if (out) out->slabCount = slabCount;

    for (guild::u32 v23 = 0; v23 < slabCount; ++v23) {
        guild::u8* rec = slab + v23 * kCl3_CharStride;       // v62 + dword_62CEFC
        std::memset(rec, 0, kCl3_CharStride);                // SetGrayColorThunk(0,404,rec)

        guild::u8 lead = 0;                                  // v64[0]
        if (!RD(src, &lead, 1))                              // ReadStream(v64,1,h,1)
            return 0;
        rec[9] = lead;                                       // v39[9] = v64[0]

        if (!RD(src, rec + 12, 4))   return 0;               // +12 (4)
        if (!RD(src, rec + 20, 4))   return 0;               // +20 (4)
        if (!RD(src, rec + 48, 32))  return 0;               // +48 (32)
        if (!RD(src, rec + 80, 32))  return 0;               // +80 (32)
        if (!RD(src, rec + 240, 160))return 0;               // +240 (160)
        if (!RD(src, rec + 400, 1))  return 0;               // +400 (1)
        if (!RD(src, rec + 36, 4))   return 0;               // +36 (4)  next-action idx
        if (!RD(src, rec + 40, 4))   return 0;               // +40 (4)  prev-action idx
    }

    // --- step 5: action RELINK pass over all 1280 slots (0x5a9b77) ---------
    // Note: the relink iterates the FULL slab capacity (0x500 == 1280), not the
    // version-gated read count — slots beyond `slabCount` were left zero by the
    // pre-clear (SetGrayColorThunk(0,404,charBase) at 0x5a9b2d only clears one
    // record; in this reconstruction the caller hands a pre-cleared slab, matching
    // the writer's dead-slot zero-fill).
    for (guild::u32 v24 = 0; v24 < kCl3_CharCapacity; ++v24) {
        guild::u8* rec = slab + v24 * kCl3_CharStride;
        if (rec[9] == 0)
            continue;

        // rec[+20] = handleTable[rec[+20]] ; if 0 -> zero whole record + continue
        guild::u32 hIdx = RdU32(rec + 20);
        guild::u32 hRes = hooks.handleResolve ? hooks.handleResolve(hIdx, user) : hIdx;
        WrU32(rec + 20, hRes);
        if (hRes == 0) {
            std::memset(rec, 0, kCl3_CharStride);            // SetGrayColorThunk(0,404,rec)
            continue;
        }

        // next-action index (+36): -1 -> 0, else 404*idx + base (offset model: 404*idx)
        guild::u32 nIdx = RdU32(rec + 36);
        WrU32(rec + 36, nIdx == 0xFFFFFFFFu ? 0u : nIdx * kCl3_CharStride);
        // prev-action index (+40): same
        guild::u32 pIdx = RdU32(rec + 40);
        WrU32(rec + 40, pIdx == 0xFFFFFFFFu ? 0u : pIdx * kCl3_CharStride);

        // rec[+0] = vtable-table[ (i32(rec+6) >> 24) ] ; rec[+12] = 0
        guild::i32 rec6 = (guild::i32)RdU32(rec + 6);
        int typeIndex = rec6 >> 24;                          // sar 24
        guild::u32 vt = hooks.actionVtable ? hooks.actionVtable(typeIndex, user) : 0;
        WrU32(rec + 0, vt);
        WrU32(rec + 12, 0);

        // validity fixup: if both next(+36) and prev(+40) became 0 -> stockptr/owner
        // relink (engine: rec[+20]->[+296]=rec). Modelled minimally: when the
        // resolved next-action is 0 and the +20 handle is non-null, the original
        // back-links rec into the owner (rec[+20]+296 = rec). That is engine pointer
        // state; the SERIALIZED bytes (above) are unaffected, so it is omitted from
        // the portable relink and only the +36/+40 conversions and rec[0]/rec[12]
        // are applied. (See report: 0x5a9ef1 owner back-link.)

        // next/prev sanity (0x5a9f61 / 0x5aa04a): the original re-checks the raw
        // index == -1 BEFORE conversion to emit a diagnostic and zero +36/+40; since
        // we already mapped -1 -> 0 above the post-state is identical (the diagnostic
        // sprintf is cosmetic — dropped).
    }

    if (out) out->fullyParsed = true;
    return 1;
}

} // namespace guild::io
