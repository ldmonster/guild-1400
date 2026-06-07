#include "test.h"

#include "render/render_leaves9.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
template <class T> void PutAt(unsigned char* p, int off, T v) { std::memcpy(p + off, &v, sizeof(T)); }
}

namespace {
// A capturing FrameDataProcess that records the draw-mode it saw and the frame
// pointer, used to wire the DrawFromBank* leaves into a small draw pipeline.
int g_seenMode = -1;
void* g_seenFrame = nullptr;
int PipelineFrameProc(int, int, void* frame, int) {
    g_seenFrame = frame;
    g_seenMode = static_cast<unsigned char*>(frame)[13];
    return 1;
}
}

// ===========================================================================
// Integration: a small "shape blob setup + draw" pipeline. A shape/bank record
// carries a palette header (count @ +10, dest-offset @ +42); we stage a palette
// via CopyPaletteHeader, copy a 3-component tint vector with Vector_Copy3, then
// drive a frame draw through DrawFromBankMode1/5 with the real-style frame proc
// wired in. This threads four leaves together the way the shape loader/renderer
// would.
// ===========================================================================
TEST(RenderLeaves9Pipeline, PaletteThenTintThenDraw) {
    RenderLeaves9Hooks h{};
    h.FrameDataProcess = PipelineFrameProc;
    h.ConvertRgbTo16 = nullptr; h.Convert8To16 = nullptr;
    SetLeaves9Hooks(h);

    unsigned char shape[512];
    std::memset(shape, 0, sizeof(shape));
    PutAt<u16>(shape, 10, 4);          // 4 palette entries -> 16 bytes
    PutAt<u32>(shape, 42, 8);          // bank entry-count u16 lives here too;
                                       //   set count later for the draw stage
    // Palette stage uses a separate header view; reuse offset fields explicitly.
    PutAt<u16>(shape, 10, 4);
    PutAt<u32>(shape, 42, 128);        // palette destination offset
    unsigned char palette[16];
    for (int i = 0; i < 16; ++i) palette[i] = static_cast<unsigned char>(i * 3 + 1);
    u32 copied = Shape_CopyPaletteHeader(palette, shape);
    CHECK_EQ(copied, 16u);
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(shape[128 + i], static_cast<unsigned char>(i * 3 + 1));

    // Stage a tint vector (RGB as floats) into the shape header area via Copy3.
    u32 tintBits[3] = {0x3F000000u /*0.5f*/, 0x3E800000u /*0.25f*/, 0x3F800000u /*1.0f*/};
    u32* dst = reinterpret_cast<u32*>(shape + 200);
    Vector_Copy3(dst, tintBits);
    CHECK_EQ(dst[0], 0x3F000000u);
    CHECK_EQ(dst[1], 0x3E800000u);
    CHECK_EQ(dst[2], 0x3F800000u);

    // Draw stage: configure the bank header for a valid index 0 -> frame @ +260.
    PutAt<u16>(shape, 42, 4);          // bank entry count
    PutAt<u32>(shape, 0 + 69, 260);    // entry 0 frame delta
    shape[260 + 13] = 0x33;            // original draw-mode byte
    g_seenMode = -1;

    CHECK_EQ(Render_RetTrue(), 1);
    i32 r = Shape_DrawFromBankMode5(shape, 0, 0, 0, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_seenMode, 5);           // mode forced to 5 during the draw
    CHECK_EQ(shape[260 + 13], 0x33);   // restored
    CHECK_EQ(g_seenFrame, shape + 260);

    ResetLeaves9Hooks();
}

// Two independent palette stages into the same blob at different offsets must
// not clobber each other (the copy count/offset come purely from the header).
TEST(RenderLeaves9Pipeline, TwoPaletteStagesAreIndependent) {
    unsigned char shape[256];
    std::memset(shape, 0, sizeof(shape));

    PutAt<u16>(shape, 10, 2);
    PutAt<u32>(shape, 42, 32);
    unsigned char palA[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    u32 nA = Shape_CopyPaletteHeader(palA, shape);
    CHECK_EQ(nA, 8u);

    PutAt<u16>(shape, 10, 3);
    PutAt<u32>(shape, 42, 64);
    unsigned char palB[12];
    for (int i = 0; i < 12; ++i) palB[i] = static_cast<unsigned char>(0xF0 + i);
    u32 nB = Shape_CopyPaletteHeader(palB, shape);
    CHECK_EQ(nB, 12u);

    // First palette still intact.
    for (int i = 0; i < 8; ++i) CHECK_EQ(shape[32 + i], static_cast<unsigned char>(i + 1));
    // Second palette present.
    for (int i = 0; i < 12; ++i) CHECK_EQ(shape[64 + i], static_cast<unsigned char>(0xF0 + i));
}
