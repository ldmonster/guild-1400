// End-to-end flow across the render_leaves2 leaves: build a 16bpp software frame,
// rasterise primitives into it, run a clipped blit, then drive the light, mesh,
// and texture-record state machines through their installable hooks.
#include "render/render_leaves2.h"
#include "render/colorformat.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
int   g_refresh = 0;
int   g_clones  = 0;
int   g_blits   = 0;
int   g_walks   = 0;
void* g_cloneOut = (void*)0x2000;
void  E2eRefresh(u32) { ++g_refresh; }
void* E2eClone(void*, i8, i8) { ++g_clones; return g_cloneOut; }
bool  E2eLock(void*) { return true; }
void  E2eBlit(void*, void*, const i32*, const i32*) { ++g_blits; }
i8    E2eWalk(void*, void*, void*, int, const u8*) { ++g_walks; return 1; }
} // namespace

TEST(RenderLeaves2_E2E, FramePrimitivesAndState) {
    // --- install hooks for the whole flow ---
    RenderLeaves2Hooks h{};
    h.beginFrameLock    = &E2eLock;
    h.refreshAllObjects = &E2eRefresh;
    h.cloneRecord       = &E2eClone;
    h.blit              = &E2eBlit;
    h.walkAndInvoke     = &E2eWalk;
    InstallRenderLeaves2Hooks(h);
    g_refresh = g_clones = g_blits = g_walks = 0;

    const int W = 16, Hh = 16;
    std::vector<u16> frame(W * Hh, 0);
    FrameTarget t{};
    t.base = (u8*)frame.data(); t.width = W; t.height = Hh; t.pitch = W;
    t.bpp = 16; t.fmt = Format565();

    // 1) Draw a horizontal scan line (row 5, x 2..9) in red.
    DrawHLine(t, 2, 5, 5, 10, 0xF800);
    for (int x = 2; x < 10; ++x) CHECK_EQ(frame[5 * W + x], (u16)0xF800);

    // 2) Plot green pixels via PutPixel (lock hook returns true).
    PutPixel(t, 8, 8, nullptr, 0xFF, 0x00, 0x00);   // g=0xFF r=0 b=0 -> green 0x07E0
    CHECK_EQ(frame[8 * W + 8], (u16)0x07E0);
    // off-screen plot is rejected (returns input x, no write)
    CHECK_EQ(PutPixel(t, 99, 8, nullptr, 0xFF, 0xFF, 0xFF), 99);

    // 3) Clipped blit: a partly off-screen source rect gets clamped, blit issued.
    BlitClipRect clip{};
    int srcSurf = 0;
    CHECK_EQ(BlitClipped(clip, -2, 3, 8, 6, &srcSurf, -1, 0, nullptr), 1);
    CHECK_EQ(clip.dstX, 1);     // a1<0: a4 += a1 then a6<0 path adjusts
    CHECK_EQ(clip.issued, true);
    CHECK_EQ(g_blits, 1);
    // a fully-clipped blit (width collapses) issues nothing
    CHECK_EQ(BlitClipped(clip, -6, 0, 4, 6, &srcSurf, 0, 0, nullptr), 0);
    CHECK_EQ(g_blits, 1);

    // 4) Light direction state machine: set + observe refresh callback.
    i32 lx = 1, ly = 2, lz = 3;
    SetGlobalDirection(&lx, &ly, &lz);
    CHECK_EQ(GlobalLight().x, 1.0f);
    CHECK_EQ(GlobalLight().z, 3.0f);
    CHECK_EQ(g_refresh, 1);

    // 5) Sun pitch is RNG-driven but bounded; "raise" stays negative.
    float pitch = 0.0f;
    SetSunHeight(&pitch, 1);
    CHECK(pitch < 0.0f);

    // 6) Texture record flag flow: clone-predicate then transparency toggle.
    u8 rec[120] = {0};
    rec[108] = 0xFF; rec[110] = 0x00;          // eligible for palette clone
    void* cloned = CloneIfPaletteMatch(rec, 7, 0);
    CHECK_EQ(cloned == g_cloneOut, true);
    CHECK_EQ(g_clones, 1);

    rec[104] = 0x00;
    CHECK_EQ(SetTransparencyFlag(rec, 1), (i8)1);
    CHECK_EQ(rec[104] & 0x04, (u8)0x04);       // transparency bit set
    CHECK_EQ(SetTransparencyFlag(rec, 1), (i8)0);  // idempotent

    // 7) PackColorFlags over the record bytes.
    rec[105] = 0x1F; rec[106] = 0x1F; rec[114] = 0x0F;
    u8 o0 = 0, o1 = 0;
    PackColorFlags(rec, &o0, &o1);
    CHECK_EQ(o1, (u8)0xFF);                    // (0x1F<<4)|0x1F

    // 8) Mesh global colour temp drives a scene-graph walk hook.
    std::vector<u8> obj(540, 0);
    obj[528] = 0x01;                           // bit0 set -> branch A
    CHECK_EQ(SetGlobalColorTemp(obj.data(), 0x10, 0x20, 0x30), (i8)1);
    CHECK_EQ(g_walks, 1);

    // 9) Mesh vertex-colour shim over an object with a zero-submesh mesh
    //    (reuses the reconstructed SetVertexColors; stamp loop runs 0 times).
    std::vector<u8> mesh(64, 0);   // mesh+12 (subCount) = 0
    std::vector<u8> obj2(540, 0);
    void* meshPtr = mesh.data();
    std::memcpy(obj2.data() + 460, &meshPtr, sizeof(void*));
    u8 rgb[3] = {0x10, 0x20, 0x30};            // non-zero -> stamp branch
    CHECK_EQ(SetVertexColorRgb(obj2.data(), rgb), (i8)1);
    CHECK_EQ(obj2[530] & 2, (u8)2);            // flag set by SetVertexColors
    CHECK_EQ(obj2[528] & 4, (u8)4);

    // 10) Bounding radius accessor round-trip.
    float r = 12.25f;
    std::memcpy(mesh.data() + 468, &r, sizeof(float));
    std::vector<u8> node(64, 0);
    std::memcpy(node.data() + 16, &meshPtr, sizeof(void*));
    CHECK_EQ(GetBoundingRadius(node.data()), 12.25);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});  // restore inert defaults
}
