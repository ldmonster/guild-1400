#pragma once
// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 1)
//
// The top save writer/loader (VIBE_Save_WriteGameFile @0x5a348c /
// VIBE_Save_LoadGameFile @0x5a7604) drive a fixed list of per-table serializers,
// each of which streams a global record array field-by-field (every field a single
// VIBE_Vfs_WriteStream / ReadStream with an explicit size, in a fixed order, some
// fields version-gated on g_saveVersion @0x649D4C). These are pure sequential
// serializers — the ideal 1:1 reimplementation target.
//
// Because the live global arrays are owned by other (sim/world) modules, each
// serializer here is parameterized over the record-array BASE pointer (and the
// element strides are the recovered constants). The byte-exact field ORDER, SIZE,
// and version GATES are reproduced verbatim from the decompilation, with +0xNN
// offset comments. Tests round-trip each serializer over an in-memory stream and
// assert byte-identical reconstruction at every recovered offset.
//
// Reconstructed in this file (the simpler scalar / fixed-grid tables):
//   VIBE_Save_WriteMapTileTable    @0x5a3fa4   (+ load: part of LoadCityRecords)
//   VIBE_Save_WriteHotkeyTable     @0x5a7520
//   VIBE_Save_LoadHotkeyTable      @0x5aba98
//   VIBE_Save_WriteMapTiles        @0x5a6258
//   VIBE_Save_LoadMapTiles         @0x5aa7a8   (version-gated row width 256/512/768)
//   VIBE_Save_WriteHistoryAndCarts @0x5a6ff4
//   VIBE_Save_LoadHistoryAndCarts  @0x5ab59c
//   VIBE_Save_WriteAmtTable        @0x5a6e1c
//   VIBE_Save_LoadAmtTable         @0x5ab3ac   (version-gated +24 gametime / +12 id)
//   VIBE_Save_WriteGameGlobals     @0x5a6324
//   VIBE_Save_LoadGameGlobals      @0x5aa8dc
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// ===========================================================================
// MapTile table — scene-node tile table (dword_13CE290, stride 67, 8192 slots).
// VIBE_Save_WriteMapTileTable @0x5a3fa4: count live slots (word @+0 != 0), write
// count, then per live slot the fields in this fixed order:
//   +0 (2) +2 (4) +6 (4) +10 (4) +14 (4) +18 (1) +19 (1) +28 (0x1F).
// ===========================================================================
constexpr int kMapTileStride   = 67;       // 0x43
constexpr int kMapTileCapacity = 8192;     // 548864 / 67
constexpr guild::u32 kMapTileScanBytes = 548864; // 67 * 8192

// Write/read the map-tile table given the array base. The record is addressed by
// raw byte offset exactly as the original. Returns true on full success.
bool SaveWriteMapTileTable(VfsHandle* h, guild::u8* tileBase);
// Loader counterpart (the read mirror used by VIBE_Save_LoadCityRecords): reads
// `count`, then `count` records into the array, marking written slots. `clear`
// (when non-null) is a 67-byte zero template stamped into each slot before fill.
bool SaveLoadMapTileTable(VfsHandle* h, guild::u8* tileBase);

// ===========================================================================
// Hotkey table — four parallel 24-entry dword columns + a header dword.
// VIBE_Save_WriteHotkeyTable @0x5a7520 / LoadHotkeyTable @0x5aba98:
//   header dword (dword_11BC1C0), then for i in 0..23: colA[i], colB[i], colC[i],
//   colD[i] (4 bytes each). Columns: dword_11BC038 / 098 / 100 / 160.
// ===========================================================================
constexpr int kHotkeyCount = 24;

struct HotkeyTable {
    guild::u32 header;         // dword_11BC1C0
    guild::u32 colA[kHotkeyCount]; // dword_11BC038
    guild::u32 colB[kHotkeyCount]; // dword_11BC098
    guild::u32 colC[kHotkeyCount]; // dword_11BC100
    guild::u32 colD[kHotkeyCount]; // dword_11BC160
};
bool SaveWriteHotkeyTable(VfsHandle* h, const HotkeyTable& t);
bool SaveLoadHotkeyTable(VfsHandle* h, HotkeyTable& t);

// ===========================================================================
// Map tiles — two NxN byte grids (heightmap dword_123D6CD+3 row base, and the
// overlay byte_1333110). VIBE_Save_WriteMapTiles @0x5a6258 always writes 768x768
// rows; VIBE_Save_LoadMapTiles @0x5aa7a8 reads a version-dependent square side:
//   < 0x1003A : 256;  < 0x10042 : 512;  else : 768.
// The overlay grid is read only when version >= 0x10036.
// Each source row stride is 768 bytes regardless of the active side.
// ===========================================================================
constexpr int kMapRowStride = 768;
guild::u32 MapTilesSide(guild::u32 version); // 256 / 512 / 768
// Write both grids (heightmap first, then overlay), 768x768 each.
bool SaveWriteMapTiles(VfsHandle* h, const guild::u8* heightBase,
                       const guild::u8* overlayBase);
// Read both grids honoring the version side + the >=0x10036 overlay gate.
bool SaveLoadMapTiles(VfsHandle* h, guild::u8* heightBase, guild::u8* overlayBase,
                      guild::u32 version);

// ===========================================================================
// History + carts. VIBE_Save_WriteHistoryAndCarts @0x5a6ff4:
//   byte_633924 (1), (dword_633928-dword_63391C) (4), (dword_633934-dword_63392C)
//   (4), byte_122DBF0[16] (1 each), then 4 cart records (stride 68):
//     cart[k]: +0 (4), then 8 x { entry +0 (4), entry +4 (1) } over +4.. step 8.
// The two deltas are stored relative to a base so the load re-adds the base.
// ===========================================================================
constexpr int kCartCount       = 4;
constexpr int kCartStride      = 68;
constexpr int kCartEntryCount  = 8;
constexpr int kHistoryFlagsLen = 16; // byte_122DBF0

struct HistoryHeader {
    guild::u8  flag;            // byte_633924
    guild::u32 base1, cur1;     // dword_63391C / dword_633928  (cur1-base1 on wire)
    guild::u32 base2, cur2;     // dword_63392C / dword_633934  (cur2-base2 on wire)
    guild::u8  flags[kHistoryFlagsLen]; // byte_122DBF0
};
// Each 68-byte cart: a leading dword, then 8 entries of {dword, byte} at +4 step 8.
bool SaveWriteHistoryAndCarts(VfsHandle* h, const HistoryHeader& hdr,
                              const guild::u8* cartBase);
bool SaveLoadHistoryAndCarts(VfsHandle* h, HistoryHeader& hdr, guild::u8* cartBase);

// ===========================================================================
// Amt (office) table — dword_11AE6B0, stride 276 (69 dwords), 24 slots.
// VIBE_Save_WriteAmtTable @0x5a6e1c / LoadAmtTable @0x5ab3ac:
//   count slots with (dword @+0 != -1), write count, then per live slot:
//     +0 (4) +8 (1) +20 (4) +38 (1) +48 (1) +52 (0x40 x16=1024) +116 (1)
//     +124 (0x98) +24 (0xE)[load: >=0x1003F else gametime default] +12 (4)[>=0x10040 else -1].
// ===========================================================================
constexpr int kAmtStride   = 276;   // 69 dwords
constexpr int kAmtCapacity = 24;    // 6624 / 276
constexpr guild::u32 kAmtScanBytes = 6624;
bool SaveWriteAmtTable(VfsHandle* h, guild::u8* amtBase);
// `gametimeDefault` is the 14-byte (qword_13CE852) default stamped at +24 when the
// version (<0x1003F) predates the persisted gametime; pass null to leave +24 as-is.
bool SaveLoadAmtTable(VfsHandle* h, guild::u8* amtBase, guild::u32 version,
                      const guild::u8* gametimeDefault);

// ===========================================================================
// Misc game globals — a long contiguous scalar block, a 16x5-dword float matrix
// (flt_1234FA0, stride 20), five 0x44-byte blocks, and an 8x8 grid of 12-byte
// records (word_12349A0). VIBE_Save_WriteGameGlobals @0x5a6324 /
// LoadGameGlobals @0x5aa8dc. The scalar block + matrix + blocks are passed as one
// opaque region per the recovered byte layout (see GameGlobalsBlock).
// ===========================================================================
// The leading scalar block: 26 dwords/short fields written in order. We model it
// as a fixed 0x68-byte buffer (the exact serialized prefix), then the matrix, then
// the five 0x44 blocks, then the grid — all reproduced field-by-field.
struct GameGlobalsBlock {
    // 26 fields, sizes per the original (mostly 4; one 4/2/4 split for qword_1235262).
    guild::u32 d910, d914, d918, d91C, d920, d924; // dword_1234910..924
    guild::u32 f928;                                // flt_1234928
    guild::u32 d92C, d930, d934;                    // dword_123492C..934
    guild::u32 f5234;                               // flt_1235234
    guild::u32 q5262_a;                             // qword_1235262 first 4
    guild::u16 q5262_b;                             // +4 (2)
    guild::u32 q5262_c;                             // +6 (4)
    guild::u32 b526C;                               // byte_123526C (4)
    guild::u32 f641DA8, f641DAC;                    // flt_641DA8/AC
    guild::u32 d5238;                               // dword_1235238
    guild::u32 b523C, b5240, b5244, b5248, b524C, b5250; // byte_123523C..250 (4 each)
    guild::u32 d5254;                               // dword_1235254
    guild::u32 b5258;                               // byte_1235258 (4)
    // 16 x 5-dword float matrix (flt_1234FA0, stride 20).
    guild::u32 matrix[16 * 5];
    // five 0x44-byte blocks (dword_12350E0, byte_1235124/168/1AC/1F0).
    guild::u8  block0[0x44];
    guild::u8  block1[0x44];
    guild::u8  block2[0x44];
    guild::u8  block3[0x44];
    guild::u8  block4[0x44];
    // 8x8 grid of records (word_12349A0): per record +0(2) +2(2) +4(1) +8(4); the
    // record stride is 24 bytes (v6+=24), the row stride 192 bytes (v8+=96 words).
    guild::u8  grid[8 * 8 * 24];
};
bool SaveWriteGameGlobals(VfsHandle* h, const GameGlobalsBlock& g);
bool SaveLoadGameGlobals(VfsHandle* h, GameGlobalsBlock& g);

} // namespace guild::io
