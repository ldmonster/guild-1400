#include "render/mesh_attach_textures.h"

#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// MeshAttachTextures — golden tests for VIBE_Mesh_AttachStockTextures (0x5d1114).
// No assets: a synthetic stock object with known verts/polys/materials + mock
// hooks. All byte offsets mirror the engine record map in mesh_attach_textures.h.
// =============================================================================
using namespace guild;
using guild::render::AttachStockTextures;
using guild::render::AttachTextureRecord;
using guild::render::MeshAttachHooks;
using guild::render::MeshAttachHooksMut;

namespace {

// Engine strides / offsets used by the implementation (kept local so the test is a
// black-box golden check against the documented layout).
constexpr int kVertStride = 80;
constexpr int kVertSrcPtr = 48;
constexpr int kVertCol0   = 64;
constexpr int kVertCol1   = 68;
constexpr int kVertFlag76 = 76;
constexpr int kVertSide77 = 77;

constexpr int kPolyStride = 64;
constexpr int kPolyV0     = 40;
constexpr int kPolyV1     = 48;
constexpr int kPolyV2     = 56;
constexpr int kPolyFlag36 = 36;
constexpr int kPolyFlag38 = 38;
constexpr int kPolyStock  = 0;
constexpr int kPolyTex    = 8;

constexpr int kHdrVertArr  = 0;
constexpr int kHdrPolyArr  = 8;
constexpr int kHdrVertCnt  = 16;
constexpr int kHdrPolyCnt  = 20;
constexpr int kHdrStock    = 24;
constexpr int kHdrTexSet   = 32;
constexpr int kHdrFlag380  = 380;
constexpr int kHdrFlag381  = 381;

// Native-width stock layout (see mesh_attach_textures.cpp stock:: — pointer fields
// moved to a reserved region to avoid 64-bit ptr overlap; counts keep engine offsets).
constexpr int kStockVertCnt = 68;
constexpr int kStockPolyCnt = 76;
constexpr int kStockRefCnt  = 476;
constexpr int kStockMatCnt  = 480;
constexpr int kStockVertArr = 540;   // ptr slot (orig +64)
constexpr int kStockPolyArr = 548;   // ptr slot (orig +72)
constexpr int kStockTexNameArr = 556;// ptr slot (orig +516)

constexpr int kSpolyIdx0  = 24;
constexpr int kSpolyTexId = 36;
constexpr int kSpolyMatId = 40;

constexpr int kStockVertStride = 24;
constexpr int kStockPolyStride = 56;

constexpr int kNodeActiveFrame = 460;
constexpr int kNodeDrawData    = 492;
constexpr int kNodeByte532     = 532;
constexpr int kDrawDataLodCnt  = 2316;
constexpr int kLodRecBase      = 244;

static inline u32 rd32(const u8* p, int off) { u32 v; std::memcpy(&v, p + off, 4); return v; }
static inline void wr32(u8* p, int off, u32 v) { std::memcpy(p + off, &v, 4); }
static inline uintptr_t rdptr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}
static inline void wrptr(u8* p, int off, uintptr_t v) { std::memcpy(p + off, &v, sizeof(v)); }

// ---- Synthetic engine fixture ----
struct Fixture {
    std::vector<u8> stock;       // stock object block
    std::vector<u8> stockVerts;  // 24-byte stock verts
    std::vector<u8> stockPolys;  // 56-byte stock polys
    std::vector<u8> drawBlock;   // a2 (header)
    std::vector<u8> drawVerts;   // 80-byte draw verts
    std::vector<u8> drawPolys;   // 64-byte draw polys
    std::vector<u8> node;        // object node
    std::vector<u8> drawData;    // node+492 block

    int nverts = 0, npolys = 0, nmats = 0;

    void build(int nv, int np, int nm) {
        nverts = nv; npolys = np; nmats = nm;
        stock.assign(600, 0);
        stockVerts.assign((std::size_t)nv * kStockVertStride, 0);
        stockPolys.assign((std::size_t)np * kStockPolyStride, 0);
        drawBlock.assign(512, 0);
        drawVerts.assign((std::size_t)(nv + 8) * kVertStride, 0);
        drawPolys.assign((std::size_t)np * kPolyStride, 0);
        node.assign(600, 0);
        drawData.assign(4000, 0);

        // Array bases are native heap pointers (stored via wrptr); counts are u32.
        wrptr(stock.data(), kStockVertArr, (uintptr_t)stockVerts.data());
        wr32(stock.data(), kStockVertCnt, (u32)nv);
        wrptr(stock.data(), kStockPolyArr, (uintptr_t)stockPolys.data());
        wr32(stock.data(), kStockPolyCnt, (u32)np);
        wr32(stock.data(), kStockMatCnt, (u32)nm);

        // node+492 -> drawData
        wrptr(node.data(), kNodeDrawData, (uintptr_t)drawData.data());
    }

    void setStockVert(int i, float x, float y, float z) {
        u8* v = stockVerts.data() + (std::size_t)i * kStockVertStride;
        std::memcpy(v + 0, &x, 4); std::memcpy(v + 4, &y, 4); std::memcpy(v + 8, &z, 4);
    }
    // stock poly i: 3 indices, texRecIdx (stockPoly+36), matIndex (stockPoly+40).
    void setStockPoly(int i, u32 i0, u32 i1, u32 i2, i32 texRecIdx, i32 matIdx) {
        u8* p = stockPolys.data() + (std::size_t)i * kStockPolyStride;
        wr32(p, kSpolyIdx0 + 0, i0);
        wr32(p, kSpolyIdx0 + 4, i1);
        wr32(p, kSpolyIdx0 + 8, i2);
        wr32(p, kSpolyTexId, (u32)texRecIdx);
        wr32(p, kSpolyMatId, (u32)matIdx);
    }
};

// A real allocPolysAndPoints that points the draw block at the fixture's arrays.
Fixture* g_fx = nullptr;
void allocFromFixture(u8* a2, int /*lodArg*/) {
    wrptr(a2, kHdrVertArr, (uintptr_t)g_fx->drawVerts.data());
    wrptr(a2, kHdrPolyArr, (uintptr_t)g_fx->drawPolys.data());
}

// Texture-record table for the mock textureRecord hook.
struct TexRecMock { u8 f104, t108, m110; uintptr_t handle; bool present; };
std::vector<TexRecMock>* g_texRecs = nullptr;
AttachTextureRecord texRecHook(int matIndex) {
    AttachTextureRecord r;
    if (g_texRecs && matIndex >= 0 && (std::size_t)matIndex < g_texRecs->size()) {
        const TexRecMock& m = (*g_texRecs)[(std::size_t)matIndex];
        r.flags104 = m.f104; r.trans108 = m.t108; r.mode110 = m.m110;
        r.handle = m.present ? m.handle : 0;
    }
    return r;
}

// Refcount recorder.
std::vector<std::pair<uintptr_t,int>>* g_refbumps = nullptr;
void refCountHook(uintptr_t h, int d) { if (g_refbumps) g_refbumps->push_back({h, d}); }

// Error recorder.
struct ErrRec { std::string fmt; std::string stock; std::string tex; bool hasTex; };
std::vector<ErrRec>* g_errs = nullptr;
void errHook(const char* fmt, const char* s, const char* t) {
    if (g_errs) g_errs->push_back({fmt ? fmt : "", s ? s : "", t ? t : "", t != nullptr});
}

u8* g_findResult = nullptr;
u8* findHook(const char* /*name*/) { return g_findResult; }

void resetHooks() {
    MeshAttachHooks& h = MeshAttachHooksMut();
    h = MeshAttachHooks{};   // back to inert defaults
    g_fx = nullptr; g_texRecs = nullptr; g_refbumps = nullptr; g_errs = nullptr;
    g_findResult = nullptr;
}

} // namespace

// (a) stock-not-found -> null.
TEST(MeshAttachTextures, StockNotFoundReturnsNull) {
    resetHooks();
    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook;  // returns g_findResult (null)
    std::vector<u8> node(600, 0), draw(512, 0);
    u8* r = AttachStockTextures(node.data(), draw.data(), 0, "MISSING");
    CHECK(r == nullptr);
    resetHooks();
}

// (b) vertex copy fills draw verts' source ptrs/flags; counts copied into header.
TEST(MeshAttachTextures, VertexCopyFillsSourcePtrsAndFlags) {
    resetHooks();
    Fixture fx; fx.build(/*nv*/3, /*np*/0, /*nm*/0);
    g_fx = &fx;
    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook; g_findResult = fx.stock.data();
    h.allocPolysAndPoints = allocFromFixture;

    u8* r = AttachStockTextures(fx.node.data(), fx.drawBlock.data(), 0, "OBJ");
    CHECK(r == fx.stock.data());
    // header counts
    CHECK_EQ(rd32(fx.drawBlock.data(), kHdrVertCnt), 3u);
    CHECK_EQ(rd32(fx.drawBlock.data(), kHdrPolyCnt), 0u);
    CHECK_EQ(rdptr(fx.drawBlock.data(), kHdrStock), (uintptr_t)fx.stock.data());
    CHECK_EQ(rd32(fx.drawBlock.data(), kHdrFlag380), 0u & 0xFFu); // +380 byte == 0
    // verts: nv+8 = 11 filled; src ptr = stockVertArray + i*24; +76=0; +64/+68=-1.
    const uintptr_t base = (uintptr_t)fx.stockVerts.data();
    for (int i = 0; i < 3 + 8; ++i) {
        u8* dv = fx.drawVerts.data() + (std::size_t)i * kVertStride;
        CHECK_EQ(rdptr(dv, kVertSrcPtr), base + (uintptr_t)i * kStockVertStride);
        CHECK_EQ((int)dv[kVertFlag76], 0);
        CHECK_EQ(rd32(dv, kVertCol0), 0xFFFFFFFFu);
        CHECK_EQ(rd32(dv, kVertCol1), 0xFFFFFFFFu);
    }
    // refcount on stock bumped.
    CHECK_EQ(rd32(fx.stock.data(), kStockRefCnt), 1u);
    resetHooks();
}

// (c) per-poly the 3 vertex ptrs resolve to base + 80*idx and get +76 |= 0x80.
TEST(MeshAttachTextures, PolyVertexPtrsResolveAndMark) {
    resetHooks();
    Fixture fx; fx.build(/*nv*/5, /*np*/1, /*nm*/0);
    fx.setStockPoly(0, /*idx*/4, 2, 0, /*texRecIdx*/-1, /*matIdx*/-1);
    g_fx = &fx;
    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook; g_findResult = fx.stock.data();
    h.allocPolysAndPoints = allocFromFixture;

    AttachStockTextures(fx.node.data(), fx.drawBlock.data(), 0, "OBJ");

    const uintptr_t vbase = (uintptr_t)fx.drawVerts.data();
    u8* dp = fx.drawPolys.data();
    CHECK_EQ(rdptr(dp, kPolyV0), vbase + (uintptr_t)4 * kVertStride);
    CHECK_EQ(rdptr(dp, kPolyV1), vbase + (uintptr_t)2 * kVertStride);
    CHECK_EQ(rdptr(dp, kPolyV2), vbase + (uintptr_t)0 * kVertStride);
    // stock poly ptr stored at draw poly slot 0 (engine +16).
    CHECK_EQ(rdptr(dp, kPolyStock), (uintptr_t)fx.stockPolys.data());
    // those 3 verts marked +76 |= 0x80.
    for (int idx : {4, 2, 0}) {
        u8* dv = fx.drawVerts.data() + (std::size_t)idx * kVertStride;
        CHECK((dv[kVertFlag76] & 0x80u) != 0);
    }
    // matIndex < 0 -> texture handle 0.
    CHECK_EQ(rdptr(dp, kPolyTex), (uintptr_t)0);
    resetHooks();
}

// (d) OPAQUE leaves no blend; MODE-1 (alpha) + MODE-2 (additive) set +64/+68 + 2-sided.
TEST(MeshAttachTextures, MaterialBlendAndTwoSided) {
    resetHooks();
    // 3 polys, 3 materials: mat0 opaque, mat1 alpha (mode1), mat2 additive(2-sided).
    Fixture fx; fx.build(/*nv*/9, /*np*/3, /*nm*/3);
    fx.setStockPoly(0, 0, 1, 2, /*texRecIdx*/0, /*matIdx*/0);
    fx.setStockPoly(1, 3, 4, 5, /*texRecIdx*/1, /*matIdx*/1);
    fx.setStockPoly(2, 6, 7, 8, /*texRecIdx*/2, /*matIdx*/2);
    g_fx = &fx;

    std::vector<TexRecMock> recs = {
        // opaque: trans108 == 0xFF, mode110 == 0, not 2-sided.
        {/*f104*/0, /*t108*/0xFF, /*m110*/0x00, /*handle*/0x100, true},
        // alpha mode1: mode110 bit0 set, trans108 = 0x40, not 2-sided.
        {/*f104*/0, /*t108*/0x40, /*m110*/0x01, /*handle*/0x200, true},
        // additive mode2: mode110 bit1 set, trans108 = 0x80, 2-sided (f104 bit0).
        {/*f104*/1, /*t108*/0x80, /*m110*/0x02, /*handle*/0x300, true},
    };
    g_texRecs = &recs;
    std::vector<std::pair<uintptr_t,int>> bumps; g_refbumps = &bumps;

    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook; g_findResult = fx.stock.data();
    h.allocPolysAndPoints = allocFromFixture;
    h.textureRecord = texRecHook;
    h.textureIncrementRefCount = refCountHook;

    AttachStockTextures(fx.node.data(), fx.drawBlock.data(), 0, "OBJ");

    const uintptr_t vbase = (uintptr_t)fx.drawVerts.data();
    auto vert = [&](int idx) { return fx.drawVerts.data() + (std::size_t)idx * kVertStride; };

    // --- mat0 OPAQUE: hasBlend false -> +64/+68 stay at the init -1 (0xFFFFFFFF).
    // (the per-vertex init set them to -1; opaque material doesn't touch them.)
    for (int idx : {0, 1, 2}) {
        CHECK_EQ(rd32(vert(idx), kVertCol0), 0xFFFFFFFFu);
        CHECK_EQ(rd32(vert(idx), kVertCol1), 0xFFFFFFFFu);
        CHECK_EQ((int)vert(idx)[kVertSide77], 0);     // f104 bit0 == 0
    }
    // poly0 +38 bit3 (2-sided) clear.
    CHECK((fx.drawPolys.data()[0 * kPolyStride + kPolyFlag38] & 0x08u) == 0);

    // --- mat1 ALPHA (mode1): v76 = all four bytes = t108 (0x40) => 0x40404040.
    for (int idx : {3, 4, 5}) {
        CHECK_EQ(rd32(vert(idx), kVertCol0), 0x40404040u);
        CHECK_EQ(rd32(vert(idx), kVertCol1), 0x40404040u);
        CHECK_EQ((int)vert(idx)[kVertSide77], 0);     // f104 bit0 == 0
    }
    CHECK((fx.drawPolys.data()[1 * kPolyStride + kPolyFlag38] & 0x08u) == 0);

    // --- mat2 ADDITIVE (mode2, NOT mode1): v76 = (t108<<24)|0xFF<<16|0xFFFF
    //     = 0x80FFFFFF; 2-sided (f104 bit0 == 1) -> +77 == 1, poly +38 bit3 set.
    const u32 expect = (0x80u << 24) | (0xFFu << 16) | 0xFFFFu;
    for (int idx : {6, 7, 8}) {
        CHECK_EQ(rd32(vert(idx), kVertCol0), expect);
        CHECK_EQ(rd32(vert(idx), kVertCol1), expect);
        CHECK_EQ((int)vert(idx)[kVertSide77], 1);
    }
    CHECK((fx.drawPolys.data()[2 * kPolyStride + kPolyFlag38] & 0x08u) != 0);

    (void)vbase;
    resetHooks();
}

// (e) texture-set copies each material's handle + bumps refcount; missing -> error path.
TEST(MeshAttachTextures, TextureSetCopyAndRefcountAndMissing) {
    resetHooks();
    // 2 materials. mat0 has a present texture (handle 0x111); mat1's texRec handle 0.
    Fixture fx; fx.build(/*nv*/6, /*np*/2, /*nm*/2);
    fx.setStockPoly(0, 0, 1, 2, /*texRecIdx*/0, /*matIdx*/0);
    fx.setStockPoly(1, 3, 4, 5, /*texRecIdx*/1, /*matIdx*/1);
    g_fx = &fx;

    std::vector<TexRecMock> recs = {
        {0, 0xFF, 0x00, /*handle*/0x111, /*present*/true},
        {0, 0xFF, 0x00, /*handle*/0x000, /*present*/false},  // missing handle
    };
    g_texRecs = &recs;
    std::vector<std::pair<uintptr_t,int>> bumps; g_refbumps = &bumps;
    std::vector<ErrRec> errs; g_errs = &errs;

    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook; g_findResult = fx.stock.data();
    h.allocPolysAndPoints = allocFromFixture;
    h.textureRecord = texRecHook;
    h.textureIncrementRefCount = refCountHook;
    h.logError = errHook;

    AttachStockTextures(fx.node.data(), fx.drawBlock.data(), 0, "STOCKY");

    // texture-set: a2[5][0] = 0x111; a2[5][1] = 0 (missing).
    u8* texSet = reinterpret_cast<u8*>(rdptr(fx.drawBlock.data(), kHdrTexSet));
    CHECK(texSet != nullptr);
    uintptr_t h0, h1;
    std::memcpy(&h0, texSet + 0 * sizeof(uintptr_t), sizeof(h0));
    std::memcpy(&h1, texSet + 1 * sizeof(uintptr_t), sizeof(h1));
    CHECK_EQ(h0, (uintptr_t)0x111);
    CHECK_EQ(h1, (uintptr_t)0);
    // refcount bumped exactly once for the present handle.
    CHECK_EQ((int)bumps.size(), 1);
    if (!bumps.empty()) {
        CHECK_EQ(bumps[0].first, (uintptr_t)0x111);
        CHECK_EQ(bumps[0].second, 1);
    }
    // error path: "texture not found" for mat1 + the "Not all Textures" summary.
    bool sawNotFound = false, sawSummary = false;
    for (const auto& e : errs) {
        if (e.fmt.find("texture %s not found") != std::string::npos &&
            e.stock == "STOCKY")
            sawNotFound = true;
        if (e.fmt.find("Not all Textures used/found") != std::string::npos)
            sawSummary = true;
    }
    CHECK(sawNotFound);
    CHECK(sawSummary);
    resetHooks();
}

// (f) LOD-frame select picks the first valid LOD record. The original checks the
// frame's a2[3] polyCount > 0 && a2[2] vertCount > 0 && a2[4] stock ptr != 0; under
// the 64-bit relocation those are the frame's hdr +20 / +16 / +24 (the offsets
// AttachStockTextures writes), not the literal engine +256/+252/+260.
TEST(MeshAttachTextures, LodFrameSelectPicksFirstValid) {
    resetHooks();
    Fixture fx; fx.build(/*nv*/1, /*np*/0, /*nm*/0);
    g_fx = &fx;
    MeshAttachHooks& h = MeshAttachHooksMut();
    h.findStockObject = findHook; g_findResult = fx.stock.data();
    h.allocPolysAndPoints = allocFromFixture;
    // selectLodFrame returns 0 -> forces the scan fallback.
    h.selectLodFrame = [](u8*) -> uintptr_t { return 0; };

    // Make LOD record 0 INVALID (zeros) and LOD record 1 VALID. Record k is at
    // drawData + 384*k + 244; the scan reads vertCount@+16, polyCount@+20, stock@+24.
    u8* dd = fx.drawData.data();
    auto setRec = [&](int k, i32 vertCnt, i32 polyCnt, uintptr_t stockPtr) {
        u8* rec = dd + (std::size_t)k * 384 + kLodRecBase;
        wr32(rec, kHdrVertCnt, (u32)vertCnt);
        wr32(rec, kHdrPolyCnt, (u32)polyCnt);
        wrptr(rec, kHdrStock, stockPtr);
    };
    setRec(0, /*vert*/0, /*poly*/0, /*stock*/0);            // invalid
    setRec(1, /*vert*/4, /*poly*/4, /*stock*/0xABCD);       // valid
    setRec(2, /*vert*/4, /*poly*/4, /*stock*/0xDEAD);       // also valid (must not win)

    // node+460 starts 0 (so the scan runs).
    AttachStockTextures(fx.node.data(), fx.drawBlock.data(), 0, "OBJ");

    // node+460 should point at drawData + 384*1 + 244.
    const uintptr_t want = (uintptr_t)(dd + 1 * 384 + kLodRecBase);
    CHECK_EQ(rdptr(fx.node.data(), kNodeActiveFrame), want);
    // node+532 set to 4; drawData+2316 lodCount incremented.
    CHECK_EQ((int)fx.node.data()[kNodeByte532], 4);
    CHECK_EQ((int)fx.drawData.data()[kDrawDataLodCnt], 1);
    resetHooks();
}

// (g) SelectLodFrameForNode (0x5adb6c raw-block adapter) — FORCED-LOD branch (the
// headless no-camera path): node+531 bits 4-5 select the LOD index ((4*flags>>6)-1),
// the chosen frame address (drawData+244+384*idx) is returned, node+528 |= 0x40 fires
// only when the frame CHANGED, and an empty chosen frame returns 0.
TEST(MeshAttachTextures, SelectLodFrameForNode_Forced) {
    using guild::render::SelectLodFrameForNode;
    using guild::render::SetLodSelectView;
    using guild::render::LodView;
    SetLodSelectView(LodView{});   // no active camera -> forced branch

    std::vector<u8> node(600, 0);
    std::vector<u8> dd(2600, 0);
    wrptr(node.data(), kNodeDrawData, (uintptr_t)dd.data());
    dd[kDrawDataLodCnt] = 4;                                   // lodCount = 4
    auto frame = [&](int k) { return dd.data() + kLodRecBase + (std::size_t)k * 384; };
    for (int k = 0; k < 4; ++k) { wr32(frame(k), kHdrVertCnt, 10); wr32(frame(k), kHdrPolyCnt, 12); }

    // flags 0x20 -> ((u8)(4*0x20) >> 6) - 1 = 2 - 1 = 1; active frame was none -> dirty.
    node[531] = 0x20; node[528] = 0; wrptr(node.data(), kNodeActiveFrame, 0);
    CHECK_EQ(SelectLodFrameForNode(node.data()), (uintptr_t)frame(1));
    CHECK((node[528] & 0x40) != 0);

    // flags 0x10 -> index 0; still changed from frame(1) (currentFrameIndex was 0? no -
    // node+460 reset to 0 below) -> dirty.
    node[531] = 0x10; node[528] = 0; wrptr(node.data(), kNodeActiveFrame, 0);
    CHECK_EQ(SelectLodFrameForNode(node.data()), (uintptr_t)frame(0));
    CHECK((node[528] & 0x40) != 0);

    // UNCHANGED: node+460 already == the chosen frame(0) -> no dirty bit set.
    node[531] = 0x10; node[528] = 0; wrptr(node.data(), kNodeActiveFrame, (uintptr_t)frame(0));
    CHECK_EQ(SelectLodFrameForNode(node.data()), (uintptr_t)frame(0));
    CHECK((node[528] & 0x40) == 0);

    // Empty chosen frame -> return 0 (the original's result+8 && result+12 gate).
    wr32(frame(1), kHdrVertCnt, 0); wr32(frame(1), kHdrPolyCnt, 0);
    node[531] = 0x20; wrptr(node.data(), kNodeActiveFrame, 0);
    CHECK_EQ(SelectLodFrameForNode(node.data()), (uintptr_t)0);
}

// (h) SelectLodFrameForNode — DISTANCE branch (a live camera supplied via the view):
// index = clamp(trunc(|obj-cam| * lodCount * fovScale), 0, lodCount-1).
TEST(MeshAttachTextures, SelectLodFrameForNode_Distance) {
    using guild::render::SelectLodFrameForNode;
    using guild::render::SetLodSelectView;
    using guild::render::LodView;

    std::vector<u8> node(600, 0);
    std::vector<u8> dd(2600, 0);
    wrptr(node.data(), kNodeDrawData, (uintptr_t)dd.data());
    dd[kDrawDataLodCnt] = 4;
    auto frame = [&](int k) { return dd.data() + kLodRecBase + (std::size_t)k * 384; };
    for (int k = 0; k < 4; ++k) { wr32(frame(k), kHdrVertCnt, 10); wr32(frame(k), kHdrPolyCnt, 12); }

    node[531] = 0;                                  // no forced bits -> distance branch
    auto setf = [&](int off, float f) { u32 b; std::memcpy(&b, &f, 4); wr32(node.data(), off, b); };
    setf(76, 10.0f); setf(80, 0.0f); setf(84, 0.0f);   // obj pos (10,0,0)

    LodView v; v.worldPresent = true; v.fovScale = 0.05f;
    v.camPos[0] = v.camPos[1] = v.camPos[2] = 0.0f;
    SetLodSelectView(v);
    // dist 10 * count 4 * 0.05 = 2.0 -> trunc 2 -> frame(2).
    wrptr(node.data(), kNodeActiveFrame, 0);
    CHECK_EQ(SelectLodFrameForNode(node.data()), (uintptr_t)frame(2));

    SetLodSelectView(LodView{});   // restore the no-camera default for other suites
}
