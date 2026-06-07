#pragma once
#include "guild/common/types.h"
#include <cstddef>

// =============================================================================
// guild::render — sprite "shape bank": a packed container of 2D sprites (shapes)
// keyed by index, with an offset table into a single contiguous blob.
//
// Faithful 1:1 reconstruction of the bank-management cluster in gilde.exe
// (ts_texture.c / d3_interface.c):
//   0x5D8330  VIBE_ShapeBank_AddShape      (append a shape, lazily init the bank)
//   0x5D843C  VIBE_ShapeBank_AppendShape   (insert at an index, shifting the table)
//   0x5D84F4  VIBE_ShapeBank_RemoveShape   (remove an index, compact the blob)
//
// THE SHAPE-BANK RECORD (recovered byte-for-byte from the three functions)
// -----------------------------------------------------------------------------
// A bank is one contiguous buffer. Header fields (byte offsets):
//   +0x00  char[8]  magic "SHAPBANK"
//   +0x08  u8       version (= 1)
//   +0x2A  u16      shapeCount        (max 0xFF; AddShape refuses at >= 255)
//   +0x2C  u16      maxWidth          (running max of added shape widths)
//   +0x2E  u16      maxHeight         (running max of added shape heights)
//   +0x30  u32      writeCursor       (next free byte in the blob; init 2117)
//   +0x34  u8       colorDepth        (taken from the first shape's +0x0C byte)
//   +0x45  u32[]    offsetTable       (per-shape byte offset into THIS buffer)
//   +0x445 u8[1024] lightTable        (copied from byte_1406530 when depth==0)
// The first shape's bytes begin at writeCursor's initial value (2117 = 0x845),
// i.e. just past the 1024-byte light table at +0x445 (0x445 + 0x400 = 0x845).
//
// THE SHAPE RECORD HEADER (the per-sprite blob the bank stores; from AddShape)
//   +0x00  u32  size       (total byte size of this shape's blob)
//   +0x06  u16  width      (a1[3])
//   +0x0A  u16  height     (a1[5])
//   +0x0C  u8   colorDepth (must match the bank's +0x34)
// (The pixel/RLE payload follows; see render/shape.h for the blit + RLE layout.)
// =============================================================================
namespace guild::render {

// Header field byte offsets within a shape-bank buffer.
namespace bank_off {
constexpr size_t kMagic       = 0x00;
constexpr size_t kVersion     = 0x08;
constexpr size_t kShapeCount  = 0x2A;
constexpr size_t kMaxWidth    = 0x2C;
constexpr size_t kMaxHeight   = 0x2E;
constexpr size_t kWriteCursor = 0x30;
constexpr size_t kColorDepth  = 0x34;
constexpr size_t kOffsetTable = 0x45;   // u32[] starts here
constexpr size_t kLightTable  = 0x445;  // 1024-byte light table
}

// Header field byte offsets within a shape blob.
namespace shape_off {
constexpr size_t kSize       = 0x00;  // u32
constexpr size_t kWidth      = 0x06;  // u16
constexpr size_t kHeight     = 0x0A;  // u16
constexpr size_t kColorDepth = 0x0C;  // u8
}

// Initial value of the write cursor (gilde.exe constant 2117) — the first shape's
// data starts here, just past the bank header + offset region + light table.
constexpr u32 kBankFirstDataOffset = 2117;

// The 1024-byte default light table copied into a depth-0 bank. The engine copied
// it from byte_1406530 (a runtime-built gray ramp). For a self-contained, testable
// reconstruction the caller supplies it; pass nullptr to leave it zeroed.
//
// gilde.exe 0x5D8330 — VIBE_ShapeBank_AddShape (__usercall eax=fn(shape@eax, bank@edx)).
//   Lazily initialises an empty bank (writes the magic, version=1, writeCursor=
//   2117, colorDepth from the shape, and the light table when depth==0). Refuses
//   if shapeCount >= 255 or the shape's depth != the bank's depth. Otherwise
//   copies the shape's `size` bytes to bank+writeCursor, records the offset in the
//   table, advances writeCursor, bumps shapeCount, and tracks max width/height.
//   Returns 1 on success, 0 on refusal.
int ShapeBankAddShape(u8* bank, const u8* shape, const u8* defaultLightTable = nullptr);

// gilde.exe 0x5D843C — VIBE_ShapeBank_AppendShape (__usercall ax=fn(shape@eax,
//   bank@edx, index@ebx)). Inserts `shape` at `index`: shifts the offset-table
//   entries [index, shapeCount) up by one, stores the new shape at writeCursor,
//   records its offset at index, advances writeCursor, bumps shapeCount, and
//   updates max width/height. Returns the (possibly updated) max height.
//   NOTE: the original does NOT re-pack the blob — it appends the bytes at the end
//   and only the offset *table* is reordered, so iteration order follows the table.
u16 ShapeBankAppendShape(const u8* shape, u8* bank, int index);

// gilde.exe 0x5D84F4 — VIBE_ShapeBank_RemoveShape (__usercall eax=fn(bank@eax,
//   index@edx)). Removes shape `index`: if it is the last (shapeCount==index) it
//   is simply zeroed out; otherwise its bytes are memmove'd out of the blob, every
//   offset-table entry that pointed past it is decremented by the removed size, the
//   table is compacted, the freed final slot is zeroed, shapeCount--, and
//   writeCursor -= removedSize. Returns 1 on success, 0 if index > shapeCount,
//   and `bank` (==0) when bank is null.
int ShapeBankRemoveShape(u8* bank, int index);

// --- Small typed accessors over the bank header (reconstruction convenience) ---
u16 ShapeBankCount(const u8* bank);
u32 ShapeBankShapeOffset(const u8* bank, int index);
const u8* ShapeBankShape(const u8* bank, int index);
u16 ShapeBankMaxWidth(const u8* bank);
u16 ShapeBankMaxHeight(const u8* bank);

} // namespace guild::render
