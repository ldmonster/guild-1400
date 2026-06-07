#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — sprite ("shape") animation: a fixed 16-slot table of playing
// animations, advanced by a global millisecond clock.
//
// Faithful 1:1 reconstruction of the shape-animation cluster in gilde.exe
// (d3_interface.c):
//   0x5D8D54  VIBE_ShapeAnim_RegisterSlot   (claim a free slot for an anim object)
//   0x5D8DFC  VIBE_ShapeAnim_OffsetSlotPos  (nudge a slot's screen position)
//   0x5D8B10  VIBE_ShapeAnim_AdvanceFrames  (tick all slots, advance frames)
//
// THE SLOT TABLE (recovered byte-for-byte)
// -----------------------------------------------------------------------------
// A global array of 16 slots, each 17 bytes (gilde.exe dword_1406420, stride 17,
// 16*17 = 272 bytes). Per-slot byte offsets:
//   +0x00  u32  object        (the anim object/clip pointer; 0 = free slot)
//   +0x04  i8   frame         (current frame index; <0 means inactive/done)
//   +0x05  u8   tick          (accumulated time toward the current frame)
//   +0x06  u16  shapeValue    (the shape id/value for the current frame)
//   +0x08  u32  flag8         (bit0 set by RegisterSlot when (mode & 1))
//   +0x0C  u8   reverse       (0 = forward playback, 1 = reverse)
//   +0x0D  u16  x             (screen x position)
//   +0x0F  u16  y             (screen y position)
//
// THE ANIM CLIP record (the `object` the slot points at): per-frame data lives at
//   *(object + 0x3E) (= +62) -> frame table base offset (relative to object)
//   frame[i] = object + frameBase + 8*i : byte[0] = shapeValue, byte[1] = duration
//   *(object + 0x42) (= +66) = loop mode (0 = loop, 1 = clamp-last, 3 = ping/reset)
//   *(object + 0x43) (= +67) = u16 frame count
// =============================================================================
namespace guild::render {

constexpr int kShapeAnimSlotCount  = 16;
constexpr int kShapeAnimSlotStride = 17;
constexpr int kShapeAnimTableBytes = kShapeAnimSlotCount * kShapeAnimSlotStride; // 272

// The anim-clip record offsets the slot logic reads through the object pointer.
namespace anim_clip_off {
constexpr int kFrameTableBase = 0x3E;  // +62 (offset to the 8-byte/frame table)
constexpr int kLoopMode       = 0x42;  // +66 (0 loop / 1 clamp / 3 reset)
constexpr int kFrameCount     = 0x43;  // +67 (u16)
}

// The 16-slot table state, gathered into one re-entrant record (replaces the
// file-scope globals dword_1406420.. so the logic is testable).
struct ShapeAnimState {
    u8 table[kShapeAnimTableBytes];  // 16 * 17-byte slots, laid out as above
    i32 clock;                       // mirrors dword_62EB3C (current ms clock)
    i32 lastClock;                   // mirrors dword_64A1CC (last-ticked clock)

    void Reset();
};

// Typed accessors over a 17-byte slot (slot index 0..15). `clip` is the byte base
// of the anim-clip record the slot's `object` points at; the slot stores it as a
// raw pointer-sized field but the engine indexes the clip relative to that base.
struct ShapeAnimSlotView {
    u8* base;  // &table[17*slot]
    u32  Object()    const;
    void SetObject(u32 v);
    i8   Frame()     const;
    void SetFrame(i8 v);
    u8   Tick()      const;
    void SetTick(u8 v);
    u16  ShapeValue()const;
    void SetShapeValue(u16 v);
    u32  Flag8()     const;
    void SetFlag8(u32 v);
    u8   Reverse()   const;
    void SetReverse(u8 v);
    u16  X()         const;
    void SetX(u16 v);
    u16  Y()         const;
    void SetY(u16 v);
};

ShapeAnimSlotView ShapeAnimSlot(ShapeAnimState& st, int slot);

// gilde.exe 0x5D8D54 — VIBE_ShapeAnim_RegisterSlot (__usercall eax=fn(x@ax, y@dx,
//   mode@cl, object@ebx)). Finds the first free slot (object==0), fills it
//   (frame=0, tick=0, x, y, shapeValue from the clip's first frame), sets flag8
//   bit when (mode & 1), and returns the slot index, or -1 if all 16 are busy.
//   `clip` is the byte base of the clip record (the object's frame table).
int ShapeAnimRegisterSlot(ShapeAnimState& st, u16 x, u16 y, u8 mode,
                          u32 objectId, const u8* clip);

// gilde.exe 0x5D8DFC — VIBE_ShapeAnim_OffsetSlotPos (__usercall eax=fn(dx@ax,
//   dy@dx, slot@ebx)). Adds (dx, dy) to the slot's stored (x, y).
//   NOTE: the original's index arithmetic mixes a 16- and 17-byte stride for the
//   two words; this reconstruction uses the consistent 17-byte slot stride that
//   RegisterSlot / AdvanceFrames use (the 16* term is a decompiler artefact of the
//   packed overlapping fields). See report.
void ShapeAnimOffsetSlotPos(ShapeAnimState& st, i16 dx, i16 dy, int slot);

// gilde.exe 0x5D8B10 — VIBE_ShapeAnim_AdvanceFrames. Ticks every active slot by
//   the elapsed clock delta (clock - lastClock, clamped to >= 1), advancing the
//   current frame when the accumulated tick exceeds the frame's duration. Honours
//   the clip's loop mode at the sequence end:
//     mode 0 -> wrap to frame 0 (loop)
//     mode 1 -> clamp at the last frame (frame = -1 marker), reverse handling
//     mode 3 -> reset to frame 0 and signal completion
//   Reverse slots count frames down and apply the same end handling at frame < 0.
//
// `clipFor(objectId)` resolves a slot's object id to the clip byte base (the
// engine dereferenced the raw pointer; we take a resolver so the table stays a
// plain value type). Pass nullptr-returning resolver to skip inactive objects.
using ShapeAnimClipResolver = const u8* (*)(u32 objectId, void* ctx);
void ShapeAnimAdvanceFrames(ShapeAnimState& st, ShapeAnimClipResolver resolve, void* ctx);

} // namespace guild::render
