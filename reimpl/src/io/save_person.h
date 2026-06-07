#pragma once
// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 2 — person/
// scene records)
//
// Two of the largest record serializers, recovered byte-for-byte:
//
//   VIBE_Save_WritePersonTable     @0x5a4134   (object/building array dword_13CE298,
//   VIBE_Save_LoadPersonTable      @0x5a8190    stride 169 / kObjectStride, 256 slots)
//
//   VIBE_Save_WriteGameStateHeader @0x5a4938   (person/scene array word_12CE910,
//   VIBE_Save_LoadGlobalCounters?  -            stride 536 / kPersonStride, 768 slots)
//
// Despite the name "PersonTable", the serializer iterates the 169-byte OBJECT array
// (the same one the "Person_*" query iterators walk — see sim/person_record.h). The
// "GameStateHeader" serializer iterates the 536-byte PERSON/scene array.
//
// As in part 1, the live arrays are owned by sim/world, so each serializer is
// parameterized over the array base; the exact field order/size and version gates
// are reproduced verbatim with +0xNN offset comments. Counter biases (-1342 /
// -1468) and pointer->id conversions are preserved.
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// ===========================================================================
// Person / object table (dword_13CE298, stride 169, 256 slots, alive byte @+0).
// ===========================================================================
constexpr int kObjStride   = 169;
constexpr int kObjCapacity = 256;
constexpr guild::u32 kObjScanBytes = 43264; // 169 * 256

// A live record at offset +113 holds a heap pointer to a 0x600-byte "plantmap"
// (64 sub-records of 24 bytes) — present only when the record's +0 byte == 30.
// The reimpl passes a 0x600-byte scratch buffer for that sub-table (or null when
// the caller knows no kind-30 records exist).
constexpr int kPlantStride   = 24;
constexpr int kPlantBytes     = 0x600; // 1536 = 64 * 24
constexpr guild::u8 kKindPlant = 30;   // *r == 30 gates the plantmap sub-table

// A trailing variable-length "extra" table (dword_1234600, 96-byte records,
// dword_1234604 count) is written after all object records. The reimpl serializes
// it from a caller-provided base+count.
constexpr int kExtra96Stride = 96;

// Per-record field order (write == load, save_person.cpp documents each offset).
// `plantBase` is the 0x600 scratch used for kind-30 records (may be null when the
// record's +0 != 30). The loader's version gates: +73 read only if v>=0x10028; a
// discard dword read if v<0x10032; the 16-byte +153 lightmap read if v>=0x10043,
// else defaulted.
bool SaveWritePersonTable(VfsHandle* h, guild::u8* objBase,
                          const guild::u8* extra96Base, guild::u32 extra96Count);
bool SaveLoadPersonTable(VfsHandle* h, guild::u8* objBase, guild::u32 version,
                         guild::u8* (*allocPlant)(void* ctx), void* allocCtx,
                         guild::u8* extra96Base, guild::u32* extra96CountOut);

// A simpler entry used by tests: serialize exactly `count` object records (no extra
// table), so a populated-world roundtrip can assert byte-equality field-by-field.
// `plantBaseFor(i)` supplies the 0x600 plantmap scratch for kind-30 record i.
bool SaveWritePersonRecords(VfsHandle* h, guild::u8* objBase, guild::u32 count);
bool SaveLoadPersonRecords(VfsHandle* h, guild::u8* objBase, guild::u32 count,
                           guild::u32 version);

// ===========================================================================
// GameState header / person-scene record table (word_12CE910, stride 536, 768
// slots, marker word @+0; -1 == free).
// ===========================================================================
constexpr int kPersStride   = 536;
constexpr int kPersCapacity = 768;
constexpr guild::u32 kPersScanBytes = 205824; // 536 * 768 (== 268 words * 768)

// Counter biases applied on write (and reversed on load).
constexpr guild::i32 kCounterBiasA = 1342; // *(r+84 dword) - 1342  (writes), + on load
constexpr guild::i32 kCounterBiasB = 1468; // *(r+396 dword) - 1468

// Header preamble of WriteGameStateHeader (before the per-record loop):
//   word_63CC5C (2), live-record count (4), two id-or-(-1) fields, an 8-entry
//   handler-id table (each entry: live ? *(ptr+4) : -1).
struct GameStateHeaderPreamble {
    guild::u16 marker63CC5C;   // word_63CC5C
    guild::u32 id6498E8;       // dword_6498E8 -> *(ptr+4) or -1
    guild::u32 id6498EC;       // dword_6498EC[0] -> *(ptr+4) or -1
    guild::u32 handlerIds[8];  // dword_6498F0[i] -> *(ptr+4) or -1
};

// Write/read a single 536-byte person/scene record exactly as WriteGameStateHeader
// does (the per-slot field list). Pointer->id slots are read from `idA/idB/idC/idD`
// (the original derives these from heap pointers at +91/+92/+95 word-index and +97);
// on load they are written back to those id slots and the rest of the record fills
// from the stream. Counter biases are applied here.
struct PersonSceneLinks {
    guild::u32 idAt91;  // *((DWORD*)r+91) ? *(that+1 byte addr... ) — see notes
    guild::u32 idAt92;  // *((DWORD*)r+92)
    guild::u32 idAt95;  // *((DWORD*)r+95)
    guild::u32 idAt97;  // *((DWORD*)r+97) -> *that
};

// Write one record (the body after the marker word is already known live). `links`
// carries the four id-or-(-1) values the original converts from heap pointers.
bool SaveWritePersonSceneRecord(VfsHandle* h, const guild::u8* rec,
                                const PersonSceneLinks& links);
// Read one record; the four link ids are returned via `linksOut`. Counter biases
// are reversed (added back) into the record's +84 / +396 dwords.
bool SaveLoadPersonSceneRecord(VfsHandle* h, guild::u8* rec,
                               PersonSceneLinks* linksOut);

// Full preamble writer/reader.
bool SaveWriteGameStateHeaderPreamble(VfsHandle* h, const GameStateHeaderPreamble& p,
                                      guild::u32 liveCount);
bool SaveLoadGameStateHeaderPreamble(VfsHandle* h, GameStateHeaderPreamble& p,
                                     guild::u32* liveCountOut);

} // namespace guild::io
