#pragma once
// gilde.exe — guild::io  (MODULE: savegame live-character / action-slab LOAD pass)
//
// 1:1 reconstruction of VIBE_Save_LoadCharacters @0x5a986c — the LOAD-side
// counterpart of VIBE_Save_WriteCityAndPersonTables' character-slab writer
// (already reconstructed in save_world_tables_save_recon.cpp). It is invoked from
// the FULL branch of VIBE_Save_LoadGameFile @0x5a7604 (after the object / amt /
// history / action-queue tables) and reads back, in exact order:
//
//   1. an i32 character-slot index count, then `count` slot records via
//      VIBE_Save_LoadCharacterSlot @0x5a96c0 (already in io/save_world_load.cpp);
//      each slot's person-id (+300 == *((u32*)slot+75)) is relinked through
//      VIBE_Person_FindRecordById and stored back at slot+300.
//   2. a SECOND i32 count, then `count` slot records the same way; this time the
//      +300 link is resolved through VIBE_GameObject_QueryFind(0,2,7,1,id).
//   3. (engine pass) a relink over the live person/scene table that creates the
//      character meshes / avatars / transport carts — OUT OF SCOPE for a portable
//      load (it drives the render universe, mesh loader and texture system); routed
//      through an inert hook and listed in the report.
//   4. the ACTION SLAB read loop: charBase (dword_62CEFC), stride 0x194 == 404,
//      read `slabCount` records where slabCount == 1280 (0x500) when the save
//      version dword_649D4C >= 0x10044, else 768 (0x300). Each record:
//        SetGrayColorThunk(0,404,rec)  -> zero the 404-byte record, then read
//          rec[+9]   (1 byte, leading/action-type)   <- via a 1-byte staging read
//          rec[+12]  (4)   rec[+20]  (4)
//          rec[+48]  (32)  rec[+80]  (32)
//          rec[+240] (160)
//          rec[+400] (1)
//          rec[+36]  (4)   rec[+40]  (4)
//      — byte-for-byte the inverse of the writer's emission order.
//   5. the ACTION RELINK pass over all 1280 slots: for each live record
//      (rec[+9] != 0):
//        rec[+20] = handleTable[rec[+20]]        (handle index -> object;
//                                                  modelled as byte-offset, like
//                                                  the writer's index<->offset)
//        rec[+36] = (rec[+36]==-1) ? 0 : 404*rec[+36] + base   (next-action)
//        rec[+40] = (rec[+40]==-1) ? 0 : 404*rec[+40] + base   (prev-action)
//        rec[+0]  = handler-thunk-table[19 * (i32(rec+6) >> 24)]  (action vtable;
//                                                  engine table dword_66FCD0 ->
//                                                  routed through a hook)
//        rec[+12] = 0
//        validity fixups (stockptr / next-prev) reproduced 1:1 below.
//
// The SERIALIZATION (every byte read, in order, version-gated) and the index<->
// offset relink arithmetic are reconstructed here byte-exact. The render/mesh/
// avatar creation, the person/object-id resolution and the action-vtable lookup
// are foreign engine state; they are reached only through CharLoadHooks, whose
// default (inert) implementation is deterministic and lets the serialization be
// golden-tested headless.
#include "guild/common/types.h"

namespace guild::io {

// --- recovered slab geometry (shared with the writer) ----------------------
constexpr guild::u32 kCl3_CharStride   = 0x194;   // 404 bytes per action record
constexpr guild::u32 kCl3_CharCapacity = 1280;    // 0x500 records in the slab
constexpr guild::u32 kCl3_CharSpan     = 0x7E400; // 517120 == 1280 * 404
// version gate for the on-disk record count (dword_649D4C):
constexpr guild::u32 kCl3_VerFullSlab  = 0x10044; // >= -> 1280 records, else 768
constexpr guild::u32 kCl3_CountHigh    = 1280;    // 0x500
constexpr guild::u32 kCl3_CountLow     = 768;     // 0x300
// the live-actor slot record (LoadCharacterSlot) is 516 bytes; its person link is
// the dword at +300 (== *((u32*)slot + 75)).
constexpr guild::u32 kCl3_SlotSize     = 516;
constexpr int        kCl3_SlotLinkOff  = 300;

// --- byte source (the rule-4 file-I/O boundary) ----------------------------
// Mirrors VIBE_Vfs_ReadStream(dst, size, h, count): returns true iff exactly
// size*count bytes were produced. A flat cursor over a buffer makes the read
// order golden-testable without the VFS.
struct CharLoadSource {
    const guild::u8* buf;
    guild::u32       cap;
    guild::u32       pos;
    bool             ok;
};
CharLoadSource CharLoadSourceOpen(const guild::u8* buffer, guild::u32 capacity);

// --- foreign engine state, routed through inert-default hooks ---------------
// All four are pure relink helpers in the original; here they default to the
// identity / null behaviour that keeps the serialization deterministic.
struct CharLoadHooks {
    // VIBE_Save_LoadCharacterSlot @0x5a96c0 — read one 516-byte slot record from
    // the source into `slot`. Returns false on short read. Default: streams the
    // serialized subset off `src` (see save_world_load LoadCharacterSlot) — but for
    // this slice we only need it to advance the cursor identically, so the default
    // reads the same field set. Set to wire the real loader.
    bool (*loadSlot)(CharLoadSource* src, guild::u8* slot, guild::u32 version,
                     void* user) = nullptr;
    // VIBE_Person_FindRecordById @0x58bc6c — id -> person record pointer (here a
    // 32-bit handle/id passthrough). Default: identity.
    guild::u32 (*findPersonById)(guild::u32 id, void* user) = nullptr;
    // VIBE_GameObject_QueryFind(0,2,7,1,id) @0x5857fc — id -> object handle. Default:
    // identity.
    guild::u32 (*findObjectById)(guild::u32 id, void* user) = nullptr;
    // handle table dword_66F0D0 indirection: idx -> object (modelled as a byte
    // offset, matching the writer). Default: identity (offset stays as-is).
    guild::u32 (*handleResolve)(guild::u32 idx, void* user) = nullptr;
    // action-vtable table dword_66FCD0[19 * typeIndex] -> rec[+0]. Default: 0.
    guild::u32 (*actionVtable)(int typeIndex, void* user) = nullptr;
    // The render/mesh/avatar relink pass (step 3). Default: no-op.
    void (*buildCharacterScene)(void* user) = nullptr;
};

// Output of the load (the portable subset the loaders populate). `slab` is the
// caller-owned 1280*404 character slab (dword_62CEFC); the read loop and relink
// pass write into it in place.
struct CharLoadResult {
    guild::i32 slotCountA = 0;   // first index count
    guild::i32 slotCountB = 0;   // second index count
    guild::u32 slabCount  = 0;   // 768 or 1280 (version-gated)
    bool       fullyParsed = false;
};

// gilde.exe 0x5a986c — VIBE_Save_LoadCharacters.
//   src      : open byte source (the gunzipped VFS stream)
//   version  : dword_649D4C (== SaveVersionGet())
//   slab     : the 1280*404 character/action slab base (dword_62CEFC), pre-cleared
//   slotScratch : a kCl3_SlotSize-byte scratch the slot reads land in (the original
//                 fetches a real slot pointer per index; for a portable load we
//                 stream into scratch so the read order is reproduced exactly)
//   hooks    : foreign-state hooks (null entries -> inert defaults)
//   user     : opaque hook context
//   out      : receives the counts / parse status
// Returns 1 on a fully-parsed slab (matching the original BOOL), 0 on any short
// read (the world is left partially populated, as in the original abort path).
int LoadCharacters(CharLoadSource* src, guild::u32 version, guild::u8* slab,
                   guild::u8* slotScratch, const CharLoadHooks& hooks, void* user,
                   CharLoadResult* out);

} // namespace guild::io
