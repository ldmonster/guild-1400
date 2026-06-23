#pragma once
// gilde.exe — guild::io  (MODULE: savegame city/person/character table writer)
//
// 1:1 reconstruction of VIBE_Save_WriteCityAndPersonTables @0x5a57f4 — the
// top-level write pass for the person table, the city-tower table and the live
// character/action slab. Called from VIBE_Save_WriteGameFile @0x5a348c.
//
// The function performs, in order (transcribed from the disassembly):
//   1. PERSON TABLE  (dword_12CEA94[], stride 0x218=536 bytes => 768 entries):
//        count non-null pointer entries; write the count (u32); for each non-null
//        entry call WriteObjectRecord(entryPtr, dword_12CE914[i] /*linkId*/).
//   2. CITY-TOWER TABLE  (index array dword_13CE290[], stride 0x43=67 bytes =>
//        8192 entries; each i16 indexes record array dword_13CE27C[], stride
//        0x41=65 bytes):  count entries whose record[0]==0x1D (29); write the count
//        (u32); for each such entry call WriteObjectRecord(*(ptr at idx+0x3B),
//        *(u32 at idx+0x02) /*linkId*/).
//   3. CHARACTER SLAB  (dword_62CEFC, stride 0x194=404 bytes => 1280 entries):
//        a PRE-PASS rewrites three pointer slots of every live (slot[+9]!=0) record
//        to serialisable forms (deref +0x14 once; +0x24/+0x28 pointer->index via
//        (ptr-base)/404, or 0xFFFFFFFF when null; dead slots are zero-filled);
//        a WRITE loop emits a 1-byte presence/leading byte + nine fixed fields per
//        record (from a zeroed-then-maybe-copied 404-byte staging buffer);
//        a POST-PASS restores the pointer slots (index->ptr; +0x14 handle relookup).
//
// Boundary handling (rules 1/4/8): the three live tables and the handle table
// (dword_66F0D0) are owned by other modules; here they are passed in as bases.
// File I/O (the rule-4 boundary) is reconstructed against a byte-buffer cursor
// (SaveTableSink). The per-object emission (VIBE_Save_WriteObjectRecord @0x5a55b0,
// already reconstructed in save_serial3) is reached through a function-pointer hook
// so this slice neither redefines it (ODR) nor approximates it; the character-record
// fields and the count/order/fixup logic ARE reconstructed byte-for-byte.
//
// The copy-path probe (IsValidPointer(handle) / *(*(handle+0x34)+0x200)!=0) walks a
// foreign slab; it is routed through CharProbeHook (default: probe fails -> leading
// byte 0 and an all-zero record body, which is fully deterministic and byte-exact).
//
// Pointer-slot width caveat: the original is 32-bit, so the character-record link
// slots (+0x14/+0x24/+0x28) are 4 bytes. To stay 4-byte (and avoid 64-bit host
// pointer overlap between the adjacent +0x24/+0x28 slots), this reconstruction
// models the slab LINK slots as a 32-bit byte-offset-into-the-slab (0 == null,
// matching the original null pointer == 0); the pre-pass converts that to the
// loader's record index (offset/stride) or -1 — byte-exact with the binary's
// (ptr-base)/stride and -1. The handle slot (+0x14) holds the 32-bit handle index
// directly (the original's `*(DWORD*)slot` deref of a pointer-to-index is the
// identity in this id-based model; see types.h: serialized paths store 32-bit ids,
// not pointers). The bytes that hit the savegame stream are byte-exact. (One corner:
// a link to slab record 0 has byte-offset 0 and is therefore indistinguishable from
// null in this model — the original distinguishes them by pointer identity; record 0
// as a link target is not used by the table writer.)
#include "guild/common/types.h"

namespace guild::io {

// --- recovered strides / counts -------------------------------------------
constexpr guild::u32 kSwt_PersonStride       = 0x218; // 536 bytes
constexpr guild::u32 kSwt_PersonSpan         = 0x64800; // 411648 -> 768 entries
constexpr guild::u32 kSwt_CityIndexStride    = 0x43;  // 67 bytes (index array entry)
constexpr guild::u32 kSwt_CityIndexSpan      = 0x86000; // 548864 -> 8192 entries
constexpr guild::u32 kSwt_CityRecordStride   = 0x41;  // 65 bytes (record array entry)
constexpr guild::u8  kSwt_CityLiveTag        = 0x1D;  // record[0]==29 means "present"
constexpr guild::u32 kSwt_CharStride         = 0x194; // 404 bytes
constexpr guild::u32 kSwt_CharSpan           = 0x7E400; // 517120 -> 1280 entries

// --- byte-buffer sink (the file-I/O boundary) ------------------------------
struct SaveTableSink {
    guild::u8* buf;
    guild::u32 cap;
    guild::u32 pos;
    bool       ok;
};
SaveTableSink SaveTableSinkOpen(guild::u8* buffer, guild::u32 capacity);

// --- table bases the writer operates over (owned elsewhere) ----------------
struct SaveTableEnv {
    // PERSON: array of 768 records, stride 536. personPtr[i] (dword at entry+0) is a
    // pointer to the object record; personLink[i] (dword_12CE914) is the link id.
    const guild::u8* personBase;   // dword_12CEA94
    const guild::u8* personLink;   // dword_12CE914 (parallel u32 link-id array, stride 536)
    // CITY: index array (8192 * 67), each entry's i16@+0 indexes the record array.
    const guild::u8* cityIndexBase; // dword_13CE290
    const guild::u8* cityRecordBase;// dword_13CE27C
    // CHARACTER slab: 1280 * 404.
    guild::u8*       charBase;      // dword_62CEFC
    // handle table (u32-indexed pointer array); slot[idx] -> object pointer.
    const guild::u8* const* handleTable; // dword_66F0D0
};

// VIBE_Save_WriteObjectRecord @0x5a55b0 hook (already reconstructed in
// save_serial3 as SaveWriteObjectRecord). Wire this to that in a real build.
// Returns true on success.
using WriteObjectRecordFn = bool (*)(SaveTableSink* s, const guild::u8* rec,
                                     guild::i32 linkId, void* user);

// Copy-path probe (the IsValidPointer / *(*(h+0x34)+0x200) test). Given the resolved
// handle pointer (handleTable[rec[+0x14]]), return true to take the COPY path
// (leading byte = rec[+9], full 404-byte record body) or false for the ZERO path
// (leading byte 0, all-zero body). Default (null) -> false.
using CharProbeHook = bool (*)(const guild::u8* handle, void* user);

// gilde.exe 0x5a57f4 — VIBE_Save_WriteCityAndPersonTables. Returns true (1) on full
// success, false (0) on any stream failure (matching the original's BOOL).
bool SaveWriteCityAndPersonTables(SaveTableSink* sink, const SaveTableEnv& env,
                                  WriteObjectRecordFn writeObjRec,
                                  CharProbeHook probe, void* user);

} // namespace guild::io
