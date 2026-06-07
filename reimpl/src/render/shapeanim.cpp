#include "render/shapeanim.h"

#include <cstring>

namespace guild::render {

namespace {
inline u16 GetU16(const u8* b) { u16 v; std::memcpy(&v, b, 2); return v; }
inline void SetU16(u8* b, u16 v) { std::memcpy(b, &v, 2); }
inline u32 GetU32(const u8* b) { u32 v; std::memcpy(&v, b, 4); return v; }
inline void SetU32(u8* b, u32 v) { std::memcpy(b, &v, 4); }
} // namespace

void ShapeAnimState::Reset() {
    std::memset(table, 0, sizeof(table));
    clock = 0;
    lastClock = 0;
}

// --- slot accessors (17-byte slot, offsets per shapeanim.h) ------------------
u32  ShapeAnimSlotView::Object()      const { return GetU32(base + 0x00); }
void ShapeAnimSlotView::SetObject(u32 v)    { SetU32(base + 0x00, v); }
i8   ShapeAnimSlotView::Frame()       const { return static_cast<i8>(base[0x04]); }
void ShapeAnimSlotView::SetFrame(i8 v)      { base[0x04] = static_cast<u8>(v); }
u8   ShapeAnimSlotView::Tick()        const { return base[0x05]; }
void ShapeAnimSlotView::SetTick(u8 v)       { base[0x05] = v; }
u16  ShapeAnimSlotView::ShapeValue()  const { return GetU16(base + 0x06); }
void ShapeAnimSlotView::SetShapeValue(u16 v){ SetU16(base + 0x06, v); }
u32  ShapeAnimSlotView::Flag8()       const { return GetU32(base + 0x08); }
void ShapeAnimSlotView::SetFlag8(u32 v)     { SetU32(base + 0x08, v); }
u8   ShapeAnimSlotView::Reverse()     const { return base[0x0C]; }
void ShapeAnimSlotView::SetReverse(u8 v)    { base[0x0C] = v; }
u16  ShapeAnimSlotView::X()           const { return GetU16(base + 0x0D); }
void ShapeAnimSlotView::SetX(u16 v)         { SetU16(base + 0x0D, v); }
u16  ShapeAnimSlotView::Y()           const { return GetU16(base + 0x0F); }
void ShapeAnimSlotView::SetY(u16 v)         { SetU16(base + 0x0F, v); }

ShapeAnimSlotView ShapeAnimSlot(ShapeAnimState& st, int slot) {
    return ShapeAnimSlotView{st.table + kShapeAnimSlotStride * slot};
}

namespace {
// frame[i] base within a clip: clip + *(clip+62) + 8*i.
inline const u8* FrameTable(const u8* clip) {
    return clip + GetU32(clip + anim_clip_off::kFrameTableBase);
}
inline u8 FrameShape(const u8* frameTable, int frame) { return frameTable[8 * frame]; }
inline u8 FrameDuration(const u8* frameTable, int frame) { return frameTable[8 * frame + 1]; }
inline u8  ClipLoopMode(const u8* clip) { return clip[anim_clip_off::kLoopMode]; }
inline u16 ClipFrameCount(const u8* clip) { return GetU16(clip + anim_clip_off::kFrameCount); }
} // namespace

// gilde.exe 0x5D8D54 — VIBE_ShapeAnim_RegisterSlot.
int ShapeAnimRegisterSlot(ShapeAnimState& st, u16 x, u16 y, u8 mode,
                          u32 objectId, const u8* clip) {
    // Find the first free slot. The original scans while slot.object != 0,
    // stopping at offset 272 (== all 16 slots) or the first empty one.
    int slotIdx = 0;
    if (ShapeAnimSlot(st, 0).Object()) {
        int off = 0;
        do {
            off += kShapeAnimSlotStride;
            ++slotIdx;
        } while (off < kShapeAnimTableBytes &&
                 GetU32(st.table + off) != 0);
    }
    if (slotIdx >= kShapeAnimSlotCount) return -1;

    ShapeAnimSlotView s = ShapeAnimSlot(st, slotIdx);
    s.SetObject(objectId);
    s.SetFrame(0);
    s.SetX(x);
    s.SetY(y);
    s.SetTick(0);
    // shapeValue = clip frame-table[ 8 * currentFrame ] with frame == 0.
    const u8* ft = FrameTable(clip);
    s.SetShapeValue(FrameShape(ft, s.Frame()));
    if (mode & 1) s.SetFlag8(1);
    return slotIdx;
}

// gilde.exe 0x5D8DFC — VIBE_ShapeAnim_OffsetSlotPos.
void ShapeAnimOffsetSlotPos(ShapeAnimState& st, i16 dx, i16 dy, int slot) {
    ShapeAnimSlotView s = ShapeAnimSlot(st, slot);
    s.SetX(static_cast<u16>(dx + static_cast<i16>(s.X())));
    s.SetY(static_cast<u16>(dy + static_cast<i16>(s.Y())));
}

// gilde.exe 0x5D8B10 — VIBE_ShapeAnim_AdvanceFrames.
void ShapeAnimAdvanceFrames(ShapeAnimState& st, ShapeAnimClipResolver resolve, void* ctx) {
    if (st.clock == st.lastClock) return;

    // delta = clamp(clock - lastClock, >= 1) as a byte (the original truncates to
    // a char: v0 = clock - lastClock, or 1 when equal — here they differ).
    i32 d = st.clock - st.lastClock;
    u8 delta = (d == 0) ? 1 : static_cast<u8>(d);
    st.lastClock = st.clock;

    for (int i = 0; i < kShapeAnimSlotCount; ++i) {
        ShapeAnimSlotView s = ShapeAnimSlot(st, i);
        if (!s.Object()) continue;
        if (s.Frame() < 0) continue;            // byte_1406424 >= 0 gate

        const u8* clip = resolve ? resolve(s.Object(), ctx) : nullptr;
        if (!clip) continue;

        s.SetTick(static_cast<u8>(s.Tick() + delta));
        const u8* ft = FrameTable(clip);
        const int frame = s.Frame();
        const u16 frameCount = ClipFrameCount(clip);
        const u8 loopMode = ClipLoopMode(clip);

        if (s.Tick() <= FrameDuration(ft, frame)) continue;

        // Advance the frame (forward when reverse==0, backward when reverse==1).
        if (s.Reverse() == 0) s.SetFrame(static_cast<i8>(s.Frame() + 1));
        if (s.Reverse() == 1) s.SetFrame(static_cast<i8>(s.Frame() - 1));
        s.SetTick(0);

        bool atEnd = false;

        // Forward end-of-sequence (frame index reached the count).
        if (s.Frame() >= static_cast<int>(frameCount) && s.Reverse() == 0) {
            if (loopMode == 0) {
                s.SetFrame(0);
                s.SetShapeValue(FrameShape(ft, s.Frame()));
            } else if (loopMode == 1) {
                s.SetFrame(-1);
                s.SetShapeValue(ft[8 * frameCount - 8]);  // last real frame
            } else if (loopMode == 3) {
                s.SetFrame(0);
                s.SetShapeValue(FrameShape(ft, s.Frame()));
                std::memset(s.base, 0, kShapeAnimSlotStride);  // SetGrayColorThunk(0,17,slot)
            }
            atEnd = true;
        }

        // Reverse end-of-sequence (frame fell below zero in reverse playback).
        if (s.Frame() < 0 && s.Reverse() == 1) {
            if (loopMode == 0) {
                s.SetFrame(static_cast<i8>(frameCount - 2));
                s.SetShapeValue(FrameShape(ft, s.Frame()));
            } else if (loopMode == 1) {
                s.SetFrame(-1);
                s.SetShapeValue(ft[0]);  // first frame byte
            } else if (loopMode == 3) {
                s.SetFrame(0);
                s.SetShapeValue(FrameShape(ft, s.Frame()));
                std::memset(s.base, 0, kShapeAnimSlotStride);
            }
            atEnd = true;
        } else if (!atEnd) {
            // Normal advance: adopt the new frame's shape value.
            s.SetShapeValue(FrameShape(ft, s.Frame()));
        }
    }
}

} // namespace guild::render
