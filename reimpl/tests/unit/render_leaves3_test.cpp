// Unit tests for guild::render render_leaves3 — golden vectors computed with
// python3 (CRC oracle: zlib.crc32; float math: IEEE single precision).
#include "render/render_leaves3.h"
#include "render/render_leaves4.h"  // g_rawLightingFlag (byte_649D70)
#include "compress/crc.h"
#include "test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

bool nearf(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Reset module globals + hooks to a known state before each test.
void resetModule() {
    MipState() = MipFilterState{};
    UvState()  = UvScrollState{};
    ViewParamState()     = ViewParams{};
    InstallRenderLeaves3Hooks(RenderLeaves3Hooks{});  // all-null -> inert defaults
}

} // namespace

// ---------------------------------------------------------------------------
// SetMipFilterLevel — power-of-two clamp + level derivation.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_SetMip, PowerOfTwoLevels) {
    resetModule();
    int cacheResets = 0;
    int filterCalls = 0;
    RenderLeaves3Hooks h{};
    h.textureCacheReset = +[](){ return 0; };
    // capture via statics:
    static int* s_cr; static int* s_fc;
    s_cr = &cacheResets; s_fc = &filterCalls;
    h.textureCacheReset    = +[](){ ++*s_cr; return 7; };
    h.computeFilterWeights = +[](float*){ ++*s_fc; };
    InstallRenderLeaves3Hooks(h);

    struct { u32 in; u32 size; u8 shift; u8 bias; } cases[] = {
        {4,   4,  4, 0},
        {8,   8,  3, 0},
        {16,  16, 2, 0},
        {64,  64, 0, 0},
        {100, 64, 0, 0},  // clamped to 64
        {128, 64, 0, 1},
        {256, 64, 0, 2},
    };
    for (auto& c : cases) {
        MipState() = MipFilterState{};
        u32 r = SetMipFilterLevel(c.in);
        CHECK_EQ((int)r, 7);  // returns textureCacheReset() result
        CHECK_EQ((int)MipState().size, (int)c.size);
        CHECK_EQ((int)MipState().shift, (int)c.shift);
        CHECK_EQ((int)MipState().lodBias, (int)c.bias);
    }
    CHECK_EQ(filterCalls, 7);
    CHECK_EQ(cacheResets, 7);
}

TEST(RenderLeaves3_SetMip, OutOfRangeNoOp) {
    resetModule();
    static int s_calls; s_calls = 0;
    RenderLeaves3Hooks h{};
    h.textureCacheReset = +[](){ ++s_calls; return 1; };
    InstallRenderLeaves3Hooks(h);
    CHECK_EQ((int)SetMipFilterLevel(3), 3);
    CHECK_EQ((int)SetMipFilterLevel(257), 257);
    CHECK_EQ((int)SetMipFilterLevel(0), 0);
    CHECK_EQ((int)MipState().size, 0);  // untouched
    CHECK_EQ(s_calls, 0);               // no cache reset on the no-op path
}

// ---------------------------------------------------------------------------
// ScrollUvCoords — additive/subtractive channels + wrap.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_ScrollUv, GoldenAfter1000Ticks) {
    resetModule();
    UvState().lastTick = 0;
    ScrollUvCoords(1000);
    CHECK_EQ(UvState().lastTick, 1000);
    // Golden values from the python oracle:
    CHECK(nearf(UvState().bankU[1],  0.03333333507180214f));
    CHECK(nearf(UvState().bankU[7],  0.30000001192092896f));
    CHECK(nearf(UvState().bankU[15], 0.6666667461395264f));
    // subtractive channel wraps once: 0 - rate[1]*1000 + 1.0
    CHECK(nearf(UvState().bankU[17], 0.9666666388511658f));
    // channel 31 uses rate[15]; 0 - 0.00166*1000 + 1.0 stays negative (single add)
    CHECK(nearf(UvState().bankV[31], -0.6666667461395264f));
}

TEST(RenderLeaves3_ScrollUv, SameTickIsNoOp) {
    resetModule();
    UvState().lastTick = 500;
    UvState().bankU[1] = 0.25f;
    ScrollUvCoords(500);  // tick == lastTick
    CHECK(nearf(UvState().bankU[1], 0.25f));
    CHECK_EQ(UvState().lastTick, 500);
}

TEST(RenderLeaves3_ScrollUv, AdditiveWrapsDownPastOne) {
    resetModule();
    UvState().bankU[7] = 0.99f;     // rate[7]*1000 = 0.3 -> 1.29 > 1.0 -> -1.0
    UvState().lastTick = 0;
    ScrollUvCoords(1000);
    CHECK(nearf(UvState().bankU[7], 0.99f + 0.30000001192092896f - 1.0f, 1e-5f));
}

// ---------------------------------------------------------------------------
// AdvanceAnimFrames — CRC-driven frame select + material rebind.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_Anim, FrameSelectAndRebind) {
    resetModule();
    // Mesh: frames=8, speed nibble=4 (div=9), refcount>0, groupId=42.
    AnimMesh mesh{};
    mesh.frames = 8; mesh.locked = 0; mesh.speedNibble = 4;
    mesh.refCount = 1; mesh.groupId = 42; mesh.handle = 0;
    AnimMesh* meshArr[1] = {&mesh};

    AnimMaterial mats[3]{};
    mats[0].slot = &mesh;  mats[0].boundMember = -1;  // matches groupId 42
    AnimMesh other{}; other.groupId = 99;
    mats[1].slot = &other; mats[1].boundMember = -1;  // no match
    mats[2].slot = nullptr; mats[2].boundMember = -1; // null slot skipped

    AnimGroup g{};
    g.memberCount = 1; g.hasBank = true; g.meshStride = 3;
    g.meshes = meshArr; g.meshCount = 1; g.materials = mats;

    // member = (crc32(0x12345678) + 900/9) % 8 == 6 (python oracle).
    InstallRenderLeaves3Hooks(RenderLeaves3Hooks{});  // default findGroupMember == frameByte
    i8 rc = AdvanceAnimFrames(0x12345678u, &g, 900u, 0);
    CHECK_EQ((int)rc, 1);
    CHECK_EQ(mats[0].boundMember, 6);   // matched -> member id 6
    CHECK_EQ(mats[1].boundMember, -1);  // group mismatch untouched
    CHECK_EQ(mats[2].boundMember, -1);  // null slot untouched
}

TEST(RenderLeaves3_Anim, GuardConditions) {
    resetModule();
    AnimGroup g{};
    // No bank -> no-op.
    g.memberCount = 1; g.hasBank = false; g.meshStride = 0;
    CHECK_EQ((int)AdvanceAnimFrames(1, &g, 100, 0), 1);
    // Bank but zero members -> no-op.
    g.hasBank = true; g.memberCount = 0;
    CHECK_EQ((int)AdvanceAnimFrames(1, &g, 100, 0), 1);
    // Null group.
    CHECK_EQ((int)AdvanceAnimFrames(1, nullptr, 100, 0), 1);
}

TEST(RenderLeaves3_Anim, SkipsLockedAndZeroSpeed) {
    resetModule();
    AnimMesh locked{}; locked.frames = 4; locked.locked = 1; locked.speedNibble = 2;
    locked.refCount = 1; locked.groupId = 5;
    AnimMesh* arr[1] = {&locked};
    AnimMaterial mat{}; mat.slot = &locked; mat.boundMember = -1;
    AnimGroup g{}; g.memberCount = 1; g.hasBank = true; g.meshStride = 1;
    g.meshes = arr; g.meshCount = 1; g.materials = &mat;
    AdvanceAnimFrames(7, &g, 50, 0);
    CHECK_EQ(mat.boundMember, -1);  // locked mesh skipped, no rebind
}

// ---------------------------------------------------------------------------
// CollectAffectedObject — append path + distance cull.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_LightCollect, AppendPathRespectsFlag) {
    resetModule();
    LightBoundBlock bb{}; bb.affected = 1;
    LightObject obj{}; obj.bound = &bb;
    LightObject* outBuf[4] = {};
    LightAccumulator acc{};
    acc.outArray = outBuf; acc.count = 0;
    CHECK_EQ((int)CollectAffectedObject(&obj, &acc), 1);
    CHECK_EQ(acc.count, 1);
    CHECK(outBuf[0] == &obj);

    // affected==0 -> not appended.
    bb.affected = 0; acc.count = 0;
    CollectAffectedObject(&obj, &acc);
    CHECK_EQ(acc.count, 0);
}

TEST(RenderLeaves3_LightCollect, DistanceCull) {
    resetModule();
    LightBoundBlock bbRef{}, bbObj{};
    LightObject ref{}; ref.bound = &bbRef; ref.cullRadius = 10.0f;
    ref.pos[0] = 0; ref.pos[1] = 0; ref.pos[2] = 0;

    LightObject obj{}; obj.bound = &bbObj; obj.kind = 0; obj.flags = 0;
    obj.radius = 1.0f; obj.srcRadius = 5.0f;
    obj.pos[0] = 3; obj.pos[1] = 4; obj.pos[2] = 0;  // dist = 5

    LightAccumulator acc{}; acc.reference = &ref; acc.outArray = nullptr; acc.count = 0;
    // cullRadius(5)+ref.cullRadius(10)=15 > dist(5) -> affected.
    CHECK_EQ((int)CollectAffectedObject(&obj, &acc), 1);
    CHECK_EQ(acc.count, 1);
    CHECK_EQ((int)bbObj.affected, 1);
    CHECK(nearf(obj.cullRadius, 5.0f));

    // radius==0 -> never affected.
    bbObj.affected = 0; acc.count = 0; obj.radius = 0.0f;
    CollectAffectedObject(&obj, &acc);
    CHECK_EQ(acc.count, 0);
    CHECK_EQ((int)bbObj.affected, 0);

    // far away -> not affected (sum <= dist).
    bbObj.affected = 0; acc.count = 0; obj.radius = 1.0f;
    obj.srcRadius = 1.0f; ref.cullRadius = 1.0f;
    obj.pos[0] = 30; obj.pos[1] = 40;  // dist = 50, sum = 2 <= 50
    CollectAffectedObject(&obj, &acc);
    CHECK_EQ(acc.count, 0);
}

// ---------------------------------------------------------------------------
// ApplyAmbient — slot index math + detach bracketing.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_ApplyAmbient, SlotIndexMath) {
    resetModule();
    static int s_switchCount; static u32 s_lastSlot; static int s_detachCount;
    s_switchCount = 0; s_lastSlot = 0xDEAD; s_detachCount = 0;
    RenderLeaves3Hooks h{};
    h.switchActiveSlot = +[](u32 slot, int, int, int){ ++s_switchCount; s_lastSlot = slot; };
    h.detachAndRelease = +[](int){ ++s_detachCount; return (i8)1; };
    InstallRenderLeaves3Hooks(h);

    const int base = 0x1000;
    // slot index 5: (base+5*0x3D8 - base)/0x3D8 == 5.
    i8 r = ApplyAmbient(/*obj*/1, /*extra*/2, base + 0x3D8 * 5, base, /*active*/3);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(s_switchCount, 2);     // enter + restore
    CHECK_EQ((int)s_lastSlot, 3);   // restored to activeSlot on the way out
    CHECK_EQ(s_detachCount, 1);
}

TEST(RenderLeaves3_ApplyAmbient, AlreadyActiveViewSkipsSwitch) {
    resetModule();
    static int s_switchCount; s_switchCount = 0;
    static int s_detach; s_detach = 0;
    RenderLeaves3Hooks h{};
    h.switchActiveSlot = +[](u32,int,int,int){ ++s_switchCount; };
    h.detachAndRelease = +[](int){ ++s_detach; return (i8)9; };
    InstallRenderLeaves3Hooks(h);
    // slotPtr == 0 (the off_649D64 sentinel model) -> no switch, returns detach al.
    i8 r = ApplyAmbient(1, 2, /*slotPtr*/0, 0x1000, 3);
    CHECK_EQ((int)r, 9);
    CHECK_EQ(s_switchCount, 0);
    CHECK_EQ(s_detach, 1);
}

TEST(RenderLeaves3_ApplyAmbient, OutOfRangeSlotBecomesMinusOne) {
    resetModule();
    static int s_switchCount; s_switchCount = 0;
    static u32 s_firstSlot; s_firstSlot = 1234;
    RenderLeaves3Hooks h{};
    h.switchActiveSlot = +[](u32 slot, int, int, int){ if (s_switchCount==0) s_firstSlot=slot; ++s_switchCount; };
    InstallRenderLeaves3Hooks(h);
    const int base = 0x1000;
    // index 64 (>=0x40) -> -1; switch is still invoked once (enter), but since
    // v5 == -1 there is NO restore call.
    ApplyAmbient(1, 2, base + 0x3D8 * 64, base, 3);
    CHECK_EQ(s_switchCount, 1);
    CHECK_EQ((int)(i32)s_firstSlot, -1);
}

// ---------------------------------------------------------------------------
// DetachClone — master clear + propagation.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_DetachClone, ClearsMasterAndReleases) {
    resetModule();
    static int s_released; s_released = 0;
    RenderLeaves3Hooks h{};
    h.releaseEntry = +[](void*){ ++s_released; };
    InstallRenderLeaves3Hooks(h);

    TexRecord rec{}; rec.master = 555; rec.groupId = 7; rec.tileOwner = 0;
    int ret = DetachClone(&rec, /*propagate*/0, nullptr, 0);
    CHECK_EQ(ret, 555);
    CHECK_EQ(rec.master, 0);
    CHECK_EQ(s_released, 1);
}

TEST(RenderLeaves3_DetachClone, NoCloneReturnsZero) {
    resetModule();
    TexRecord rec{}; rec.master = 0;
    CHECK_EQ(DetachClone(&rec, 1, nullptr, 0), 0);
    CHECK_EQ(DetachClone(nullptr, 1, nullptr, 0), 0);
}

TEST(RenderLeaves3_DetachClone, PropagatesToMatchingPool) {
    resetModule();
    static int s_released; s_released = 0;
    RenderLeaves3Hooks h{};
    h.releaseEntry = +[](void*){ ++s_released; };
    InstallRenderLeaves3Hooks(h);

    // master record (tile owner) + 2 pool instances, one matching group id.
    TexRecord pool[3]{};
    pool[0].master = 11; pool[0].groupId = 7; pool[0].tileOwner = 1; pool[0].refCount = 1;
    pool[1].master = 22; pool[1].groupId = 7; pool[1].refCount = 1;  // matches -> recursed
    pool[2].master = 33; pool[2].groupId = 8; pool[2].refCount = 1;  // no match

    int ret = DetachClone(&pool[0], /*propagate*/1, pool, 3);
    CHECK_EQ(ret, 11);
    CHECK_EQ(pool[0].master, 0);
    CHECK_EQ(pool[1].master, 0);   // recursed detach cleared it
    CHECK_EQ(pool[2].master, 33);  // untouched (group mismatch)
    // releaseEntry: pool[0] + pool[1] (the recursive matching instance) == 2.
    CHECK_EQ(s_released, 2);
}

// ---------------------------------------------------------------------------
// CreateTileRecord — record init + bitfield packing + mip mask.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_CreateTile, InitFieldsAndMask) {
    resetModule();
    static char s_buf[256];
    // The engine hands out a cleared record slot (FindActiveRecord returns freed
    // / zeroed storage); the +104/+106 flag bytes are read-modify-written from 0.
    std::memset(s_buf, 0, sizeof(s_buf));
    RenderLeaves3Hooks h{};
    h.findActiveRecord = +[](int, int* idx) -> void* { if (idx) *idx = 1; return s_buf; };
    // default strNCopyPad clone copies + NUL-pads.
    InstallRenderLeaves3Hooks(h);

    // 0x5db955 — the original gates the whole init on byte_649D70 (g_rawLightingFlag);
    // a clear flag short-circuits to a null return even when a slot was handed out.
    g_rawLightingFlag = 0;
    u32 counter0 = 100;
    CHECK(CreateTileRecord("tile.bmp", 16, 1, 0, 0x1F, 0, &counter0, 77) == nullptr);
    CHECK_EQ((int)counter0, 100);          // body skipped, counter untouched

    g_rawLightingFlag = 1;
    u32 counter = 100;
    char* rec = CreateTileRecord("tile.bmp", /*size*/16, /*fmt*/1, /*extra*/0,
                                 /*flag1*/0x1F, /*flag2*/0, &counter, /*batch*/77);
    CHECK(rec == s_buf);
    CHECK_EQ((int)counter, 101);
    auto dw = [rec](int n){ int v; std::memcpy(&v, rec + n * 4, 4); return v; };
    CHECK_EQ(dw(29), 16);                 // size
    CHECK_EQ(dw(30), 16);                 // size
    CHECK_EQ(dw(16), 1);                  // init marker
    CHECK_EQ(dw(20), -1);                 // owner id reset
    CHECK_EQ(dw(27), 77);                 // batch tag
    CHECK_EQ(dw(19), (16 - 1) | (16 * 16 - 1));  // 0xFF mip mask
    CHECK_EQ(std::strcmp(rec, "tile.bmp"), 0);
    // flag byte +104: bit0 = fmt&1 (1); then &0xF9|4&0xF7| (8 since flag2==0).
    // start 0 -> |1 ->1; &0xF9 -> 1; |4 -> 5; &0xF7 -> 5; |8 -> 13.
    CHECK_EQ((int)(unsigned char)rec[104], 13);
    // format byte +106 low 5 bits = flag1 & 0x1F = 0x1F.
    CHECK_EQ((int)(rec[106] & 0x1F), 0x1F);
}

// ---------------------------------------------------------------------------
// UnlinkObjectNode — doubly-linked-list splice with sentinels.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_Unlink, MiddleNode) {
    resetModule();
    // list: A <-> B <-> C ; unlink B.
    UnlinkNode a{}, b{}, c{};
    UnlinkOwner owner{};
    a.next = &b; b.prev = &a; b.next = &c; c.prev = &b;
    b.owner = &owner;
    UnlinkNode* res = UnlinkObjectNode(&b, &owner, nullptr);
    // prev(A) != head sentinel -> A->fwdBack = next(C).
    CHECK(a.fwdBack == &c);
    // next(C) != tail sentinel -> C->revBack = prev(A). result = prev(A).
    CHECK(c.revBack == &a);
    CHECK(res == &a);
}

TEST(RenderLeaves3_Unlink, HeadNodeUpdatesOwner) {
    resetModule();
    // B is the head (prev == nullptr sentinel); next C.
    UnlinkNode b{}, c{};
    UnlinkOwner owner{};
    b.prev = nullptr; b.next = &c; c.prev = &b;
    UnlinkObjectNode(&b, &owner, nullptr);
    CHECK(owner.headLink == &c);   // owner->+164 = next
    CHECK(c.revBack == nullptr);   // next->+780 = prev (== head sentinel/null)
}

TEST(RenderLeaves3_Unlink, TailNodeUpdatesOwnerAndView) {
    resetModule();
    // B is the tail (next == nullptr sentinel); prev A. Owner is the active view.
    UnlinkNode a{}, b{};
    UnlinkOwner owner{};
    owner.viewFieldA = 0xAA; owner.viewFieldB = 0xBB;
    a.next = &b; b.prev = &a; b.next = nullptr;
    int va = 0, vb = 0;
    UnlinkNode* res = UnlinkObjectNode(&b, &owner, &owner, &va, &vb);
    CHECK(owner.tailLink == &a);   // owner->+168 = prev
    CHECK(a.fwdBack == nullptr);   // prev->+776 = next (== tail sentinel/null)
    CHECK(res == &a);
    CHECK_EQ(va, 0xAA);            // view cache refreshed from owner +41
    CHECK_EQ(vb, 0xBB);            // owner +42
}

// ---------------------------------------------------------------------------
// IsSurfaceLost / SurfaceReleaseTexture / GetViewParam accessors.
// ---------------------------------------------------------------------------
TEST(RenderLeaves3_Misc, IsSurfaceLost) {
    resetModule();
    // mode==0 (GDI) -> never lost.
    CHECK(IsSurfaceLost(0, true) == true);
    // no device -> "lost" (true).
    CHECK(IsSurfaceLost(1, false) == true);
    // device present, not lost -> the || short-circuits to (false==false)==true.
    static bool s_lost; s_lost = false;
    RenderLeaves3Hooks h{};
    h.surfaceLost = +[](){ return s_lost; };
    InstallRenderLeaves3Hooks(h);
    CHECK(IsSurfaceLost(1, true) == true);   // surfaceLost()==false -> ==false -> true
    s_lost = true;
    CHECK(IsSurfaceLost(1, true) == false);  // surfaceLost()==true  -> ==false -> false
}

TEST(RenderLeaves3_Misc, SurfaceReleaseTexture) {
    resetModule();
    static int s_rel; s_rel = 0;
    RenderLeaves3Hooks h{};
    h.releaseEntry = +[](void*){ ++s_rel; };
    InstallRenderLeaves3Hooks(h);
    CHECK_EQ(SurfaceReleaseTexture(0), 0);
    CHECK_EQ(s_rel, 0);
    CHECK_EQ(SurfaceReleaseTexture(42), 42);
    CHECK_EQ(s_rel, 1);
}

TEST(RenderLeaves3_Misc, ViewParamAccessors) {
    resetModule();
    ViewParamState().a = 1; ViewParamState().b = 2; ViewParamState().c = 3; ViewParamState().d = 4;
    ViewParamState().e = 5; ViewParamState().f = 6; ViewParamState().g = 7;
    CHECK_EQ(GetViewParamA(), 1);
    CHECK_EQ(GetViewParamB(), 2);
    CHECK_EQ(GetViewParamC(), 3);
    CHECK_EQ(GetViewParamD(), 4);
    CHECK_EQ(GetViewParamE(), 5);
    CHECK_EQ(GetViewParamF(), 6);
    CHECK_EQ(GetViewParamG(), 7);
}

// CRC reuse sanity: CrcCompute(0,...) must equal the engine's VIBE_Util_Crc32.
TEST(RenderLeaves3_Misc, CrcOracle) {
    resetModule();
    u32 h = 0x12345678u;
    u8 b[4]; std::memcpy(b, &h, 4);
    CHECK_EQ((long long)compress::CrcCompute(0, b, 4), (long long)0xaf6d87d2u);
}
