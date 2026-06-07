#pragma once
#include "guild/common/types.h"
#include <cstddef>
#include <string>
#include <vector>

// =============================================================================
// guild::render — gilde.gfx archive loader + depth-2 shape decoder.
//
// The shipped art archive `gfx/gilde.gfx` is a flat directory of named records,
// each pointing at a SHAPBANK container (render/shapebank.h) of one or more
// shapes.  The menu artwork (#1773 _MENUE_BACKGROUND 800x600, #174 _BUTTON_RED
// 12/100x33, #1776 _MAIN_MENU_RAHMEN 300x320) are all "depth 2" shapes.
//
// ARCHIVE FORMAT (verified against the real 1806-record gilde.gfx)
// -----------------------------------------------------------------------------
//   +0           u32 count
//   +4           count * 84-byte records:
//                  +0   char name[48]   (NUL-padded)
//                  +48  u32  dataOffset (absolute file offset of the SHAPBANK blob)
//                  +56  u32  dataSize
//                  +80  u16  width
//                  +82  u16  height
//   Data blobs are concatenated after the header (first blob = 4 + count*84).
//
// THE DEPTH-2 SHAPE CODEC — the real menu shapes are NOT drawn by
// VIBE_Shape_ShowFromBank @0x5d861c (it RETURNS at depth==2); the live draw path
// is VIBE_FrameData_Process @0x5d781c -> VIBE_FrameTable_Index @0x5fbb24
// (reached from VIBE_Animation_Basic @0x5d85b8 via the widget/entity walk).
//
// VIBE_FrameTable_Index @0x5fbb24 (disasm-verified):
//   The per-row stream is located through a row-offset TABLE: a u32 array at
//     shape + (u32 @ shape+0x2A), one entry per row, each a byte offset relative
//     to the shape.  Row r begins at  shape + rowTable[r].
//   Each row:  u32 runCount,  then runCount runs of:
//       u32 skipBytes        ; `add edi,[esi]` — dest byte advance.  In the FILE
//                            ; the pixels are 3 bytes each, so the transparent
//                            ; gap is skipBytes/3 pixels.
//       u32 lenPixels        ; `mov ecx,[esi+4]` — opaque pixel count.
//       lenPixels * { u8 R, u8 G, u8 B }   ; 24-bit pixels (the original engine
//                            ; converts these to its native 16bpp at load — the
//                            ; `bt ecx,0` odd-byte / `ecx>>1` word loop in the
//                            ; disasm operate on the converted runtime copy; the
//                            ; on-disk record is plain 3-byte RGB).
//   `runCount` and the row table reset every row, so a leading single byte never
//   desyncs the next row.  Pixel value 0 (transparent) does not appear as an
//   explicit code — gaps are encoded as `skipBytes`.
// =============================================================================
namespace guild::shim { class IFileSystem; }

namespace guild::render {

// One archive directory entry (the 84-byte record fields we use).
struct GfxRecord {
    std::string name;        // +0  record name ("_MENUE_BACKGROUND", ...)
    u32  dataOffset = 0;     // +48 absolute file offset of the SHAPBANK blob
    u32  dataSize   = 0;     // +56 blob byte size
    u16  width      = 0;     // +80 record width
    u16  height     = 0;     // +82 record height
};

// A decoded shape: tightly-packed 32bpp 0xAARRGGBB pixels (A=0xFF opaque, A=0 for
// the transparent gaps), row-major, `width * height` entries.
struct DecodedShape {
    int width  = 0;
    int height = 0;
    std::vector<u32> argb;   // size = width*height; 0x00000000 == transparent
    int opaque = 0;          // number of opaque pixels written (diagnostics)
};

// The loaded archive: the directory + the raw file bytes (blobs are decoded on
// demand straight out of `bytes`, mirroring the engine's in-place SHAPBANK use).
class GfxArchive {
public:
    // Parse `bytes` (a whole gilde.gfx image) into the directory. Returns false
    // (and leaves the archive empty) if the header/records are malformed.
    bool LoadFromMemory(std::vector<u8> bytes);

    // Read `path` (e.g. "gfx/gilde.gfx") through `fs` and parse it. Returns false
    // if the file is missing or malformed.
    bool LoadFromFile(shim::IFileSystem& fs, const char* path);

    bool ok() const { return ok_; }
    std::size_t recordCount() const { return records_.size(); }
    const GfxRecord& record(std::size_t i) const { return records_[i]; }

    // Look up a record index by name (exact match). Returns -1 if absent.
    int FindByName(const char* name) const;

    // How many shapes the SHAPBANK blob for record `index` holds (u16 @blob+0x2A),
    // 0 if `index` is out of range / the blob is too small.
    int ShapeCount(int index) const;

    // Decode shape `shapeNr` of record `index` (depth-2 codec above) into `out`.
    // Returns false on any range/format error (out is left empty). 8bpp/depth<=1
    // shapes are not handled here (the menu art is all depth 2).
    bool DecodeShape(int index, int shapeNr, DecodedShape& out) const;

    // Convenience: decode shape `shapeNr` of the record named `name`.
    bool DecodeShapeByName(const char* name, int shapeNr, DecodedShape& out) const;

private:
    bool ok_ = false;
    std::vector<u8> bytes_;
    std::vector<GfxRecord> records_;
};

// Low-level decoder over a flat SHAPBANK blob (no archive needed) — used by the
// archive and directly by unit tests that build a synthetic blob. `blob`/`blobLen`
// is one SHAPBANK container; decodes shape `shapeNr` into `out`. Returns false on
// any range error. This is the byte-faithful reconstruction of the
// VIBE_FrameTable_Index @0x5fbb24 row-table + per-row RLE.
bool DecodeShapeBlob(const u8* blob, std::size_t blobLen, int shapeNr,
                     DecodedShape& out);

} // namespace guild::render
