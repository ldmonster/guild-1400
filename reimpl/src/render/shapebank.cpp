#include "render/shapebank.h"

#include <cstring>

// gilde.exe ts_texture.c — shape-bank management. The originals access the bank
// purely through raw byte offsets; we mirror every offset with a typed helper so
// the arithmetic stays 1:1 while remaining readable.
namespace guild::render {

namespace {

inline u16  GetU16(const u8* b, size_t off) { u16 v; std::memcpy(&v, b + off, 2); return v; }
inline void SetU16(u8* b, size_t off, u16 v) { std::memcpy(b + off, &v, 2); }
inline u32  GetU32(const u8* b, size_t off) { u32 v; std::memcpy(&v, b + off, 4); return v; }
inline void SetU32(u8* b, size_t off, u32 v) { std::memcpy(b + off, &v, 4); }

// VIBE_Util_MemMove @0x5D9310 — a forward/overlap-safe copy (std::memmove here).
inline void MemMove(u8* dst, const u8* src, size_t n) { std::memmove(dst, src, n); }

// Offset-table slot address: bank + 0x45 + 4*index (the `a2 + 4*idx + 69` idiom).
inline size_t TableSlot(int index) { return bank_off::kOffsetTable + 4u * index; }

} // namespace

// gilde.exe 0x5D8330 — VIBE_ShapeBank_AddShape.
int ShapeBankAddShape(u8* bank, const u8* shape, const u8* defaultLightTable) {
    const u16 count = GetU16(bank, bank_off::kShapeCount);
    // if (*(u16*)(bank+42) >= 0xFF) return 0;
    if (count >= 0xFFu) return 0;

    const u8 shapeDepth = shape[shape_off::kColorDepth];

    // Lazy init: when the write cursor is 0 OR there are no shapes yet, stamp the
    // header. (Matches `if (!*(bank+48) || !*(u16*)(bank+42))`.)
    if (GetU32(bank, bank_off::kWriteCursor) == 0 || count == 0) {
        SetU32(bank, bank_off::kWriteCursor, kBankFirstDataOffset);  // 2117
        std::memcpy(bank + bank_off::kMagic, "SHAPBANK", 8);
        bank[bank_off::kVersion] = 1;
        bank[bank_off::kColorDepth] = shapeDepth;
        if (shapeDepth == 0) {
            // qmemcpy(bank+1093, byte_1406530, 0x400) — the default light table.
            if (defaultLightTable)
                std::memcpy(bank + bank_off::kLightTable, defaultLightTable, 0x400);
            else
                std::memset(bank + bank_off::kLightTable, 0, 0x400);
        }
    }

    // Depth must match the bank's depth.
    if (shapeDepth != bank[bank_off::kColorDepth]) return 0;

    const u32 cursor = GetU32(bank, bank_off::kWriteCursor);
    const u32 size   = GetU32(shape, shape_off::kSize);

    // qmemcpy(bank+cursor, shape, size); record offset; advance cursor.
    std::memcpy(bank + cursor, shape, size);
    SetU32(bank, TableSlot(count), cursor);
    SetU32(bank, bank_off::kWriteCursor, cursor + size);

    // Track running max width/height.
    const u16 w = GetU16(shape, shape_off::kWidth);
    if (w > GetU16(bank, bank_off::kMaxWidth)) SetU16(bank, bank_off::kMaxWidth, w);
    const u16 h = GetU16(shape, shape_off::kHeight);
    if (h > GetU16(bank, bank_off::kMaxHeight)) SetU16(bank, bank_off::kMaxHeight, h);

    SetU16(bank, bank_off::kShapeCount, static_cast<u16>(count + 1));
    return 1;
}

// gilde.exe 0x5D843C — VIBE_ShapeBank_AppendShape (insert at `index`).
u16 ShapeBankAppendShape(const u8* shape, u8* bank, int index) {
    const u16 count = GetU16(bank, bank_off::kShapeCount);

    // Shift the offset-table tail [index, count) up by one entry:
    //   MemMove(table+4*(index+1), table+4*index, 4*(count - index)).
    MemMove(bank + TableSlot(index + 1), bank + TableSlot(index),
            4u * (count - index));

    const u32 cursor = GetU32(bank, bank_off::kWriteCursor);
    SetU32(bank, TableSlot(index), cursor);

    const u32 size = GetU32(shape, shape_off::kSize);
    std::memcpy(bank + cursor, shape, size);
    SetU32(bank, bank_off::kWriteCursor, cursor + size);

    SetU16(bank, bank_off::kShapeCount, static_cast<u16>(count + 1));

    const u16 w = GetU16(shape, shape_off::kWidth);
    if (w > GetU16(bank, bank_off::kMaxWidth)) SetU16(bank, bank_off::kMaxWidth, w);
    u16 h = GetU16(shape, shape_off::kHeight);
    if (h > GetU16(bank, bank_off::kMaxHeight)) {
        SetU16(bank, bank_off::kMaxHeight, h);
    }
    return h;
}

// gilde.exe 0x5D84F4 — VIBE_ShapeBank_RemoveShape.
int ShapeBankRemoveShape(u8* bank, int index) {
    if (!bank) return 0;  // `return result` with result==0

    const u16 count = GetU16(bank, bank_off::kShapeCount);
    if (count < index) return 0;

    const u32 removedOffset = GetU32(bank, TableSlot(index));
    u8* shapePtr = bank + removedOffset;
    const u32 size = GetU32(shapePtr, 0);  // *v4 = shape size

    if (count == index) {
        // Last shape: zero its bytes in place (SetGrayColorThunk with fill byte 0).
        std::memset(shapePtr, 0, size);
    } else {
        // Shift the blob down over the removed shape:
        //   MemMove(v4, v4+size, writeCursor - removedOffset).
        MemMove(shapePtr, shapePtr + size,
                GetU32(bank, bank_off::kWriteCursor) - removedOffset);
    }

    // Decrement every offset that pointed past the removed shape.
    for (int i = 0; i < count; ++i) {
        const u32 o = GetU32(bank, TableSlot(i));
        if (o > removedOffset) SetU32(bank, TableSlot(i), o - size);
    }

    // Compact the offset table: MemMove(table+4*index, table+4*(index+1),
    //   4*(count - index)).
    MemMove(bank + TableSlot(index), bank + TableSlot(index + 1),
            4u * (count - index));

    // Zero the now-unused final slot, decrement count, shrink the write cursor.
    SetU16(bank, bank_off::kShapeCount, static_cast<u16>(count - 1));
    SetU32(bank, TableSlot(GetU16(bank, bank_off::kShapeCount)), 0);
    SetU32(bank, bank_off::kWriteCursor,
           GetU32(bank, bank_off::kWriteCursor) - size);
    return 1;
}

u16 ShapeBankCount(const u8* bank) { return GetU16(bank, bank_off::kShapeCount); }
u32 ShapeBankShapeOffset(const u8* bank, int index) { return GetU32(bank, TableSlot(index)); }
const u8* ShapeBankShape(const u8* bank, int index) {
    return bank + GetU32(bank, TableSlot(index));
}
u16 ShapeBankMaxWidth(const u8* bank) { return GetU16(bank, bank_off::kMaxWidth); }
u16 ShapeBankMaxHeight(const u8* bank) { return GetU16(bank, bank_off::kMaxHeight); }

} // namespace guild::render
