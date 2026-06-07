// Unit tests for guild::render render_leaves2 (gilde.exe gfx/light/texture/mesh
// leaves). Golden vectors computed independently with python3 (see the source
// addresses in render_leaves2.h).
#include "render/render_leaves2.h"
#include "render/colorformat.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// --------------------------------------------------------------------------
// PackColorFlags (0x5dae38) — pure bitfield repack.
// --------------------------------------------------------------------------
TEST(RenderLeaves2_PackColorFlags, GoldenVectors) {
    auto run = [](u8 c104, u8 c105, u8 c106, u8 c114, u8& o0, u8& o1) {
        u8 rec[120] = {0};
        rec[104] = c104; rec[105] = c105; rec[106] = c106; rec[114] = c114;
        PackColorFlags(rec, &o0, &o1);
    };
    u8 o0, o1;
    run(0xC1, 0x1F, 0x1F, 0x0F, o0, o1);  CHECK_EQ(o0, (u8)0x3D); CHECK_EQ(o1, (u8)0xFF);
    run(0x00, 0x00, 0x00, 0x00, o0, o1);  CHECK_EQ(o0, (u8)0x00); CHECK_EQ(o1, (u8)0x00);
    run(0xFF, 0x15, 0x0A, 0x07, o0, o1);  CHECK_EQ(o0, (u8)0x5F); CHECK_EQ(o1, (u8)0x5A);
}

TEST(RenderLeaves2_PackColorFlags, NullSafe) {
    u8 rec[120] = {0}; u8 out = 0xAB;
    PackColorFlags(nullptr, &out, &out);  CHECK_EQ(out, (u8)0xAB);  // no write
    PackColorFlags(rec, nullptr, &out);   CHECK_EQ(out, (u8)0xAB);
    PackColorFlags(rec, &out, nullptr);   CHECK_EQ(out, (u8)0xAB);  // out1 null -> no write
}

// --------------------------------------------------------------------------
// PutPixel (0x435748) — frame target write with clip + lock hook.
// --------------------------------------------------------------------------
static bool g_lockOK = true;
static bool TestLock(void*) { return g_lockOK; }

TEST(RenderLeaves2_PutPixel, ClipAndLock16) {
    RenderLeaves2Hooks h{};
    h.beginFrameLock = &TestLock;
    InstallRenderLeaves2Hooks(h);

    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    FrameTarget t{};
    t.base = (u8*)buf; t.width = 8; t.height = 8; t.pitch = 8; t.bpp = 16;
    t.fmt = Format565();

    g_lockOK = true;
    // r=0x12 g=0x34 b=0x56 -> RGB565 0x11AA
    CHECK_EQ(PutPixel(t, 3, 4, nullptr, 0x34, 0x12, 0x56), 1);
    CHECK_EQ(buf[4 * 8 + 3], (u16)0x11AA);

    // clip: negative / >= dims returns input x, no write
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(PutPixel(t, -1, 4, nullptr, 0xFF, 0xFF, 0xFF), -1);
    CHECK_EQ(PutPixel(t, 8, 4, nullptr, 0xFF, 0xFF, 0xFF), 8);
    CHECK_EQ(buf[0], (u16)0);

    // lock fails -> returns 0, no write
    g_lockOK = false;
    CHECK_EQ(PutPixel(t, 1, 1, nullptr, 0xFF, 0xFF, 0xFF), 0);
    CHECK_EQ(buf[1 * 8 + 1], (u16)0);
    g_lockOK = true;

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});  // restore inert defaults
}

TEST(RenderLeaves2_PutPixel, Bpp8GreyAnd32) {
    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});  // default lock returns true

    u8 buf8[64] = {0};
    FrameTarget t8{};
    t8.base = buf8; t8.width = 8; t8.height = 8; t8.pitch = 8; t8.bpp = 8;
    t8.fmt = Format565();
    // 8bpp grey = (b + r + g)/3 ; g=30 r=60 b=90 -> 60
    CHECK_EQ(PutPixel(t8, 2, 1, nullptr, 30, 60, 90), 1);
    CHECK_EQ(buf8[1 * 8 + 2], (u8)60);

    u32 buf32[64] = {0};
    FrameTarget t32{};
    t32.base = (u8*)buf32; t32.width = 8; t32.height = 8; t32.pitch = 8; t32.bpp = 32;
    t32.fmt = Format565();
    CHECK_EQ(PutPixel(t32, 0, 0, nullptr, 0x34, 0x12, 0x56), 1);
    CHECK_EQ(buf32[0], (u32)0x11AA);  // packed same as 16bpp formula
}

// --------------------------------------------------------------------------
// DrawHLine (0x4351d8) — horizontal / vertical / diagonal.
// --------------------------------------------------------------------------
TEST(RenderLeaves2_DrawHLine, Horizontal) {
    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    FrameTarget t{};
    t.base = (u8*)buf; t.pitch = 8;
    // y0==y1 horizontal run from x0=2 to x1=5 on row 3 (count = x1-x0 = 3)
    DrawHLine(t, 2, 3, 3, 5, 0xABCD);
    CHECK_EQ(buf[3 * 8 + 2], (u16)0xABCD);
    CHECK_EQ(buf[3 * 8 + 3], (u16)0xABCD);
    CHECK_EQ(buf[3 * 8 + 4], (u16)0xABCD);
    CHECK_EQ(buf[3 * 8 + 5], (u16)0);     // x1 exclusive
}

TEST(RenderLeaves2_DrawHLine, Vertical) {
    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    FrameTarget t{};
    t.base = (u8*)buf; t.pitch = 8;
    DrawHLine(t, 4, 1, 4, 4, 0x1234);     // x0==x1==4, y 1..4
    CHECK_EQ(buf[1 * 8 + 4], (u16)0x1234);
    CHECK_EQ(buf[2 * 8 + 4], (u16)0x1234);
    CHECK_EQ(buf[3 * 8 + 4], (u16)0x1234);
    CHECK_EQ(buf[4 * 8 + 4], (u16)0);     // y1 exclusive
}

TEST(RenderLeaves2_DrawHLine, Diagonal45) {
    u16 buf[64];
    std::memset(buf, 0, sizeof(buf));
    FrameTarget t{};
    t.base = (u8*)buf; t.pitch = 8;
    DrawHLine(t, 0, 0, 4, 4, 0x5555);     // 45-degree (0,0)->(4,4)
    CHECK_EQ(buf[0 * 8 + 0], (u16)0x5555);
    CHECK_EQ(buf[1 * 8 + 1], (u16)0x5555);
    CHECK_EQ(buf[2 * 8 + 2], (u16)0x5555);
    CHECK_EQ(buf[3 * 8 + 3], (u16)0x5555);
    CHECK_EQ(buf[4 * 8 + 4], (u16)0x5555);
}

// --------------------------------------------------------------------------
// BlitClipped (0x423ab8) — blit-rect origin clip math.
// --------------------------------------------------------------------------
static int g_blits = 0;
static i32 g_lastDst[4], g_lastSrc[4];
static void TestBlit(void*, void*, const i32 d[4], const i32 s[4]) {
    ++g_blits;
    for (int i = 0; i < 4; ++i) { g_lastDst[i] = d[i]; g_lastSrc[i] = s[i]; }
}

TEST(RenderLeaves2_BlitClipped, NoClipNeeded) {
    RenderLeaves2Hooks h{};
    h.blit = &TestBlit;
    InstallRenderLeaves2Hooks(h);
    g_blits = 0;

    BlitClipRect out{};
    int src = 0;
    // dstX=10 dstY=20 h=30 w=40 srcX=5 srcY=6, all non-negative -> unchanged
    CHECK_EQ(BlitClipped(out, 10, 20, 30, 40, &src, 5, 6, nullptr), 1);
    CHECK_EQ(out.dstX, 10); CHECK_EQ(out.dstY, 20);
    CHECK_EQ(out.w, 40);    CHECK_EQ(out.h, 30);
    CHECK_EQ(out.srcX, 5);  CHECK_EQ(out.srcY, 6);
    CHECK_EQ(out.issued, true);
    CHECK_EQ(g_blits, 1);
    // dst rect = [x,y,x+w,y+h]; src rect = [sx,sy,sx+w,sy+h]
    CHECK_EQ(g_lastDst[0], 10); CHECK_EQ(g_lastDst[2], 50); CHECK_EQ(g_lastDst[3], 50);
    CHECK_EQ(g_lastSrc[0], 5);  CHECK_EQ(g_lastSrc[2], 45); CHECK_EQ(g_lastSrc[3], 36);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}

TEST(RenderLeaves2_BlitClipped, NegativeOriginClamp) {
    RenderLeaves2Hooks h{};
    h.blit = &TestBlit;
    InstallRenderLeaves2Hooks(h);
    g_blits = 0;

    BlitClipRect out{};
    int src = 0;
    // dstX=-3 dstY=-2 h=10 w=8 srcX=-4 srcY=-1
    //   a2<0: a3 += a2 (10-2=8), a2=0
    //   a1<0: a4 += a1 (8-3=5),  a1=0
    //   a6<0: a1 -= a6 (0+4=4),  a4 += a6 (5-4=1), srcX=0
    //   a7<0: a2 -= a7 (0+1=1),  a3 += a7 (8-1=7), srcY=0
    CHECK_EQ(BlitClipped(out, -3, -2, 10, 8, &src, -4, -1, nullptr), 1);
    CHECK_EQ(out.dstX, 4);  CHECK_EQ(out.dstY, 1);
    CHECK_EQ(out.w, 1);     CHECK_EQ(out.h, 7);
    CHECK_EQ(out.srcX, 0);  CHECK_EQ(out.srcY, 0);
    CHECK_EQ(g_blits, 1);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}

TEST(RenderLeaves2_BlitClipped, CollapsedSpan) {
    RenderLeaves2Hooks h{};
    h.blit = &TestBlit;
    InstallRenderLeaves2Hooks(h);
    g_blits = 0;

    BlitClipRect out{};
    int src = 0;
    // width clamps to 0 (dstX=-8, w=8 -> w=0) -> returns 0, no blit
    CHECK_EQ(BlitClipped(out, -8, 0, 5, 8, &src, 0, 0, nullptr), 0);
    CHECK_EQ(out.w, 0);
    CHECK_EQ(out.issued, false);
    CHECK_EQ(g_blits, 0);

    // null source surface -> still valid clip (returns 1) but no blit issued
    CHECK_EQ(BlitClipped(out, 0, 0, 5, 5, nullptr, 0, 0, nullptr), 1);
    CHECK_EQ(out.issued, false);
    CHECK_EQ(g_blits, 0);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}

// --------------------------------------------------------------------------
// SetTransparencyFlag (0x5db694) — bit-2 set/clear of rec[104].
// --------------------------------------------------------------------------
TEST(RenderLeaves2_SetTransparencyFlag, BitMath) {
    u8 rec[120] = {0};
    rec[104] = 0x00;
    CHECK_EQ(SetTransparencyFlag(rec, 1), (i8)1);  CHECK_EQ(rec[104], (u8)0x04);  // set
    CHECK_EQ(SetTransparencyFlag(rec, 1), (i8)0);  CHECK_EQ(rec[104], (u8)0x04);  // no change
    CHECK_EQ(SetTransparencyFlag(rec, 0), (i8)1);  CHECK_EQ(rec[104], (u8)0x00);  // clear
    rec[104] = 0x09;
    CHECK_EQ(SetTransparencyFlag(rec, 1), (i8)1);  CHECK_EQ(rec[104], (u8)0x0D);  // 0x09|0x04
    CHECK_EQ(SetTransparencyFlag(nullptr, 1), (i8)0);
}

// --------------------------------------------------------------------------
// CloneIfPaletteMatch (0x5dbde0) — predicate + clone hook.
// --------------------------------------------------------------------------
static int   g_cloneCalls = 0;
static void* g_cloneSentinel = (void*)0x1000;
static void* TestClone(void*, i8, i8) { ++g_cloneCalls; return g_cloneSentinel; }

TEST(RenderLeaves2_CloneIfPaletteMatch, Predicate) {
    RenderLeaves2Hooks h{};
    h.cloneRecord = &TestClone;
    InstallRenderLeaves2Hooks(h);

    u8 rec[120] = {0};
    g_cloneCalls = 0;

    rec[108] = 0x00;                                       // !=0xFF -> no clone
    CHECK_EQ(CloneIfPaletteMatch(rec, 5, 0) == rec, true);
    CHECK_EQ(g_cloneCalls, 0);

    rec[108] = 0xFF; rec[110] = 0x00;                      // eligible
    CHECK_EQ(CloneIfPaletteMatch(rec, 5, 0) == g_cloneSentinel, true);
    CHECK_EQ(g_cloneCalls, 1);

    rec[110] = 0x01;                                       // bit0 set -> no clone
    CHECK_EQ(CloneIfPaletteMatch(rec, 5, 0) == rec, true);
    CHECK_EQ(g_cloneCalls, 1);

    rec[110] = 0x00;                                       // pal==-1 & bit clear -> no clone
    CHECK_EQ(CloneIfPaletteMatch(rec, -1, 0) == rec, true);
    CHECK_EQ(g_cloneCalls, 1);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}

// --------------------------------------------------------------------------
// SetGlobalDirection (0x43ea0c) — stores global dir + refresh hook.
// --------------------------------------------------------------------------
static int g_refreshCalls = 0;
static u32 g_refreshMode = 0;
static void TestRefresh(u32 m) { ++g_refreshCalls; g_refreshMode = m; }

TEST(RenderLeaves2_SetGlobalDirection, StoresAndRefreshes) {
    RenderLeaves2Hooks h{};
    h.refreshAllObjects = &TestRefresh;
    InstallRenderLeaves2Hooks(h);
    g_refreshCalls = 0;

    i32 x = 3, y = -7, z = 11;
    CHECK_EQ(SetGlobalDirection(&x, &y, &z), 1);
    CHECK_EQ(GlobalLight().x, 3.0f);
    CHECK_EQ(GlobalLight().y, -7.0f);
    CHECK_EQ(GlobalLight().z, 11.0f);
    CHECK_EQ(g_refreshCalls, 1);
    CHECK_EQ(g_refreshMode, (u32)1);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}

// --------------------------------------------------------------------------
// SetSunHeight (0x4b24b0) — RNG-scaled pitch within recovered bounds.
// RandomFloatScaled() in [0,1) -> raise: [-0.9,-0.3], down: [0.3,0.9].
// --------------------------------------------------------------------------
TEST(RenderLeaves2_SetSunHeight, RangeAndNullSlot) {
    for (int i = 0; i < 64; ++i) {
        float up = 12345.0f, down = 12345.0f;
        CHECK_EQ(SetSunHeight(&up, 1), (i8)1);
        CHECK_EQ(SetSunHeight(&down, 0), (i8)1);
        CHECK(up   >= -0.9001f && up   <= -0.2999f);
        CHECK(down >=  0.2999f && down <=  0.9001f);
    }
    CHECK_EQ(SetSunHeight(nullptr, 1), (i8)1);  // null slot: no crash
}

// --------------------------------------------------------------------------
// GetBoundingRadius (0x5d329c) — accessor obj->(+16)->(+468 float).
// --------------------------------------------------------------------------
TEST(RenderLeaves2_GetBoundingRadius, Accessor) {
    std::vector<u8> mesh(512, 0);
    float radius = 7.5f;
    std::memcpy(mesh.data() + 468, &radius, sizeof(float));

    std::vector<u8> obj(64, 0);
    void* meshPtr = mesh.data();
    std::memcpy(obj.data() + 16, &meshPtr, sizeof(void*));

    CHECK_EQ(GetBoundingRadius(obj.data()), 7.5);
    CHECK_EQ(GetBoundingRadius(nullptr), 0.0);

    std::vector<u8> obj2(64, 0);
    CHECK_EQ(GetBoundingRadius(obj2.data()), 0.0);  // null mesh ptr -> 0
}

// --------------------------------------------------------------------------
// SetGlobalColorTemp (0x428a10) — payload + walk-hook dispatch.
// --------------------------------------------------------------------------
static int   g_walks = 0;
static u8    g_payload[3];
static void* g_walkRoot = nullptr;
static i8 TestWalk(void*, void* root, void*, int mask, const u8* p) {
    ++g_walks;
    g_walkRoot = root;
    for (int i = 0; i < 3; ++i) g_payload[i] = p[i];
    CHECK_EQ(mask, 511);
    return 1;
}

TEST(RenderLeaves2_SetGlobalColorTemp, PayloadAndWalk) {
    RenderLeaves2Hooks h{};
    h.walkAndInvoke = &TestWalk;
    InstallRenderLeaves2Hooks(h);
    g_walks = 0;

    // Branch A: obj+528 bit0 set -> walk with obj+496 untouched
    std::vector<u8> obj(540, 0);
    obj[528] = 0x01;
    u32 sentinel = 0xCAFEBABE;
    std::memcpy(obj.data() + 496, &sentinel, sizeof(sentinel));
    CHECK_EQ(SetGlobalColorTemp(obj.data(), /*b=*/0x10, /*g=*/0x20, /*r=*/0x30), (i8)1);
    CHECK_EQ(g_walks, 1);
    CHECK_EQ(g_walkRoot == obj.data(), true);
    // payload = {b, r, g}
    CHECK_EQ(g_payload[0], (u8)0x10);  // b
    CHECK_EQ(g_payload[1], (u8)0x30);  // r
    CHECK_EQ(g_payload[2], (u8)0x20);  // g
    u32 after;
    std::memcpy(&after, obj.data() + 496, sizeof(after));
    CHECK_EQ(after, sentinel);         // untouched in branch A

    // Branch B: bit0 clear -> obj+496 zeroed during walk, restored after
    std::vector<u8> obj2(540, 0);
    std::memcpy(obj2.data() + 496, &sentinel, sizeof(sentinel));
    CHECK_EQ(SetGlobalColorTemp(obj2.data(), 1, 2, 3), (i8)1);
    CHECK_EQ(g_walks, 2);
    u32 after2;
    std::memcpy(&after2, obj2.data() + 496, sizeof(after2));
    CHECK_EQ(after2, sentinel);        // restored

    // null obj -> 0, no walk
    CHECK_EQ(SetGlobalColorTemp(nullptr, 1, 1, 1), (i8)0);
    CHECK_EQ(g_walks, 2);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});
}
