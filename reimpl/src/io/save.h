#pragma once
// gilde.exe — guild::io  (MODULE: savegames + world serialization)
//
// "Die Gilde" savegames (`.cty` city seed, `.SAV` save, `.SRV` server save) are
// plain VFS streams: there is NO whole-file compression — every field is written
// raw, one-by-one, in a fixed order, via VIBE_Vfs_WriteStream, and read back in the
// same order via VIBE_Vfs_ReadStream. The format is version-gated by a 32-bit
// magic/version word `g_saveVersion` (dword_649D4C @0x649D4C), valid range
// 0x10025..0x10045. The loader accepts 0x10026..0x10045; the writer always emits
// the current version 0x10045.
//
// Reconstructed entry points (this slice — the self-contained, byte-exact core):
//   VIBE_Save_WriteScenarioBlock     @0x5a372c  (header: magic+meta+thumbnail)
//   VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0  (version-gated header reader)
//   VIBE_Save_WriteGameFile          @0x5a348c  (top writer: header + scalar block + tables)
//   VIBE_Save_LoadGameFile           @0x5a7604  (top loader: header + scalar block + tables)
//   VIBE_Save_RelinkPersonObjects    @0x5a3d8c  (pre-save pointer->id, slot fixup)
//   VIBE_Save_RelinkPersonRecords    @0x5a3e4c  (post-save id->pointer restore)
//   VIBE_Save_RelinkLoadedPointers   @0x5abb84  (post-load id->pointer, type-tagged)
//
// The per-table serializers (WritePersonTable, WriteGameStateHeader, ...) touch
// dozens of game-global tables that are owned by other (not-yet-reconstructed)
// modules; they are out of scope for this slice and listed in the report. What is
// recovered here is the load-bearing skeleton: the header layout + version table,
// the fixed field-by-field scalar block order, and the pointer<->id relink passes.
//
// I/O reuses the VFS stream layer (io/vfs); the header struct is recovered
// byte-for-byte from the reader/writer with explicit +0xNN offset comments.
#include "guild/common/types.h"
#include "io/vfs.h"
#include <cstddef>

namespace guild::io {

// --- version table (recovered from the read/write gates) -------------------
// dword_649D4C @0x649D4C — the live save version word. The writer sets it to
// kSaveVersionCurrent; the reader stores the file's version here and then gates
// every optional field on it.
enum SaveVersion : guild::u32 {
    kSaveVersionWriter  = 0x10045, // VIBE_Save_WriteScenarioBlock writes 65605
    kSaveVersionCurrent = 0x10045, // == g_saveVersion default @0x649D4C
    kSaveVersionLoadMin = 0x10026, // VIBE_Save_LoadGameFile rejects < 0x10026
    kSaveVersionLoadMax = 0x10045, // ... and > 0x10045
    kSaveVersionMin     = 0x10025, // VIBE_Save_LoadHeaderAndThumbnail floor

    // optional-field gates (the version a field first appears at):
    kVerOptUnk6477A8    = 0x10022, // scalar block: byte_6477A8[4]
    kVerExtraByte49     = 0x10033, // header +49 extra byte
    kVerGameTime50      = 0x10028, // header +50 gametime[14] + scenario block
    kVerWealthIds       = 0x1002B, // header +92/+96 id pair (else -1)
    kVerThumbExtra100   = 0x10034, // header +100 u32[8]
    kVerField132        = 0x10038, // header +132 u32 (else 2)
    kVerName136         = 0x10039, // header +136 char[96] (else "Savegame")
    kVerGlobal649894    = 0x10030, // scalar block: dword_649894[4]
    kVerGlobal632240    = 0x1003D, // scalar block: dword_632240 (else 1000000)
    kVerHandlerIds      = 0x10017, // relink: dword_6498F0[8] handler id table
    kVerAmtAemter       = 0x10045, // Amt_LoadAemter placement moves at 0x10045
};

// --- recovered header layout -----------------------------------------------
// SaveHeader is the in-memory scratch buffer the reader fills (VIBE_Save_-
// LoadHeaderAndThumbnail reads into &dword_13CEC90; the writer assembles the
// equivalent fields). Byte offsets are taken directly from the +0xNN slot reads.
// The 0xE100 thumbnail is NOT part of this struct — it is streamed through a
// temporary buffer and decoded into the live thumbnail image, so only its
// presence/size is modelled here.
//
// The original is a flat byte buffer (&dword_13CEC90) the reader fills at FIXED
// +0xNN offsets — naturally aligned, with padding gaps before the 4-byte fields
// (e.g. wealth at +0x54, the id pair at +0x5C/+0x60, field132 at +0x84, name96 at
// +0x88, addressed in the original as *((_DWORD*)a2+21/23/24/33) and a2+136). The
// struct reproduces those exact offsets (pads model the gaps); the STREAM itself
// is written/read field-by-field and is therefore contiguous (no gaps on disk).
struct SaveHeader {
    guild::u32 magic;             // +0x00  version/magic (0x10025..0x10045)
    guild::u8  flagByte;          // +0x04  mode flags (bit1 = network/partial)
    char       name[32];          // +0x05  scenario / city name (NUL-padded)
    guild::u8  pad25[3];          // +0x25  alignment gap (ts is 8-wide blob @+0x28)
    guild::u8  timestamp[8];      // +0x28  8-byte timestamp blob
    guild::u8  season;            // +0x30  season byte
    guild::u8  extraByte;         // +0x31  (>=0x10033)
    guild::u8  gameTime[14];      // +0x32  14-byte game-time (>=0x10028)
    char       scenarioTag[16];   // +0x40  "SCENARIO"+pad, or live person block
    guild::u8  flagA;             // +0x50  (>=0x10028)
    guild::u8  flagB;             // +0x51
    guild::u8  flagC;             // +0x52
    guild::u8  pad53;             // +0x53  alignment (wealth is 4-aligned @+0x54)
    guild::u32 wealth;            // +0x54  display wealth
    guild::u8  flagD;             // +0x58
    guild::u8  byte649D50;        // +0x59  byte_649D50 — written/read UNGATED after
                                  //        flagD (was previously omitted by the
                                  //        reader, causing a 1-byte header under-read)
    guild::u8  pad5A[2];          // +0x5A  alignment (idA 4-aligned @+0x5C)
    guild::u32 idA;               // +0x5C  (#23) entity id or -1 (>=0x1002B)
    guild::u32 idB;               // +0x60  (#24) entity id or -1
    guild::u32 thumbExtra[8];     // +0x64  (>=0x10034) 8 dwords
    guild::u32 field132;          // +0x84  (#33) (>=0x10038, else 2)
    char       name96[96];        // +0x88  (>=0x10039, else "Savegame")
};

// Size of the on-disk RGB thumbnail blob (160 x 120 x 3 bytes wide field;
// 0xE100 = 57600 bytes). Decoded into the live thumbnail image word_13CED76.
constexpr guild::u32 kThumbnailBytes = 0xE100;
// Thumbnail geometry (the writer's nested 120 x 160 unpack loop).
constexpr int kThumbWidth  = 160;
constexpr int kThumbHeight = 120;

// --- module-global version word (mirrors dword_649D4C) ---------------------
// VIBE_Save_WriteScenarioBlock sets this on write; LoadHeaderAndThumbnail sets it
// from the file on read. Exposed for the version-gated readers/writers and tests.
guild::u32  SaveVersionGet();
void        SaveVersionSet(guild::u32 v);

// --- header writer / reader ------------------------------------------------
// VIBE_Save_WriteScenarioBlock @0x5a372c — write the header to an open VFS stream:
// the magic, a 1-byte mode flag, the 32-byte name, an 8-byte timestamp, a season
// byte, an extra byte, the 14-byte gametime, the 16-byte scenario tag, three flag
// bytes, a wealth dword, a flag byte, and a thumbnail. Returns true on success.
// This faithful slice writes the header fields (it does NOT regenerate the live
// 160x120 framebuffer; the thumbnail bytes come from `thumbnail` or are zeroed).
bool SaveWriteScenarioBlock(VfsHandle* h, const SaveHeader& hdr,
                            const guild::u8* thumbnail /*0xE100 bytes or null*/);

// VIBE_Save_LoadHeaderAndThumbnail @0x5a7af0 — read + version-gate the header from
// an open VFS stream into `hdr`. If `thumbnail` is non-null it receives the raw
// 0xE100 thumbnail bytes (when present for the file's version). Returns true on a
// fully-parsed header. Sets the module version word from the file.
bool SaveLoadHeaderAndThumbnail(VfsHandle* h, SaveHeader& hdr,
                                guild::u8* thumbnail /*0xE100 bytes or null*/);

// --- top-level scalar block (the fixed field-by-field order) ---------------
// The 12 / 13 scalar fields written by VIBE_Save_WriteGameFile @0x5a348c right
// after the header, and read back by VIBE_Save_LoadGameFile @0x5a7604. Modelled
// as a struct so the e2e test can round-trip the determinism invariant without
// owning the game globals.
struct SaveScalarBlock {
    guild::u32 g649890;        // dword_649890
    guild::u32 g632244;        // dword_632244
    guild::u8  gameTime[14];   // qword_13CE852 (14 bytes)
    guild::u8  season;         // byte_6477A1
    guild::u32 g6498E4;        // dword_6498E4 (+4 on write: player id)
    guild::u32 g64771C;        // dword_64771C
    guild::u32 g647720;        // dword_647720
    guild::u32 g647724;        // dword_647724
    guild::u32 unk6477A8;      // byte_6477A8[4]  (>=0x10022)
    guild::u8  gB56450[24];    // dword_B56450 (24 bytes)
    guild::u32 g649894;        // dword_649894   (>=0x10030)
    guild::u32 g632240;        // dword_632240   (>=0x1003D, else 1000000)
};

// VIBE_Save_WriteGameFile scalar phase @0x5a3501.. — write the scalar block in the
// exact field order. Returns true on success.
bool SaveWriteScalarBlock(VfsHandle* h, const SaveScalarBlock& blk);

// VIBE_Save_LoadGameFile scalar phase @0x5a76d6.. — read the scalar block,
// honoring the version gates (unk6477A8 only if version>=0x10022; g649894 only if
// >=0x10030; g632240 read if >=0x1003D else defaulted to 1000000). Returns true on
// success. Must be called after SaveLoadHeaderAndThumbnail so the version is set.
bool SaveLoadScalarBlock(VfsHandle* h, SaveScalarBlock& blk);

// --- pointer<->id relink (the type-tagged relink table) --------------------
// The engine keeps a relink table: 32768 entries x 10 bytes. Each entry has a
// type tag byte (byte_B5FB61) at +1 and a 32-bit pointer/id (dword_B5FB66) at +6
// within the 10-byte record. On save, live pointers are converted to ids; on load,
// ids are converted back to pointers, dispatched by the type tag:
//   1 = Person, 2 = Building, 3 = Object, 9 = Cutscene slot.
// This is the load-bearing relink mechanism (VIBE_Save_RelinkLoadedPointers tail
// @0x5abd8d / VIBE_Save_RelinkPersonRecords @0x5a3e4c).
constexpr int      kRelinkEntryCount  = 32768;
constexpr guild::u32 kRelinkStride    = 10;     // bytes per entry
constexpr guild::u32 kRelinkTagOffset = 1;      // byte_B5FB61 within the 10 bytes
constexpr guild::u32 kRelinkPtrOffset = 6;      // dword_B5FB66 within the 10 bytes
constexpr guild::u32 kRelinkBytes     = 327680; // 32768 * 10 (loop bound)

enum RelinkTag : guild::u8 {
    kRelinkPerson   = 1,
    kRelinkBuilding = 2,
    kRelinkObject   = 3,
    kRelinkCutscene = 9,
};

// A resolver maps a 32-bit id to a 32-bit pointer-token (or 0/-1 if unresolved),
// for each of the four tagged record kinds. In the original these are
// VIBE_Person_FindRecordById / Building_FindById / Object_FindObjectById /
// Cutscene_FindSlotById. The reimpl injects them so the relink pass stays testable
// without owning those modules.
struct RelinkResolvers {
    guild::u32 (*person)(guild::u32 id, void* ctx);
    guild::u32 (*building)(guild::u32 id, void* ctx);
    guild::u32 (*object)(guild::u32 id, void* ctx);
    guild::u32 (*cutscene)(guild::u32 id, void* ctx);
    void*      ctx;
};

// VIBE_Save_RelinkLoadedPointers tail @0x5abd8d — id -> pointer fixup over the
// relink table, dispatched by tag. `table` is the 327680-byte (32768x10) blob.
void RelinkTableIdsToPointers(guild::u8* table, const RelinkResolvers& r);

// VIBE_Save_RelinkPersonRecords @0x5a3e4c (tail) — the same loop using the same
// resolvers; on the save path the engine calls these to restore pointers after a
// write. Modelled identically (resolvers map id->pointer). Provided for symmetry.
void RelinkTableRestore(guild::u8* table, const RelinkResolvers& r);

} // namespace guild::io
