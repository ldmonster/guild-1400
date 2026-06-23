// Golden vectors for render::anim_object — the object/skeletal anim attach +
// stock cluster (VIBE_Anim_CreateObjectAnim/AttachToBone/PruneExpiredAttachments
// /FreeObjAnimData/FindFreeMeshSlot/LoadStreamToStock + StrCmpNoCase +
// AdvanceFrameIndex). gilde.exe 0x5cef14 / 0x5d0b64 / 0x5d0d38 / 0x5cec00 /
// 0x5cf114 / 0x5d3858 / 0x5cb8f0 / 0x5ccf18.
#include "render/anim_object.h"
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild::render;

namespace {
AnimStream* MakeStream(const char* name, int meshType, int frameTotal) {
    auto* s = new AnimStream();
    s->name = name;
    s->meshType = meshType;
    s->frameTotal = frameTotal;
    s->loDefault = 0;
    s->hiDefault = frameTotal - 1;
    return s;
}
struct StockGuard { ~StockGuard() {
    for (auto* s : AnimStock()) delete s;
    ResetAnimStock();
} };
}  // namespace

// --- StrCmpNoCase (0x5cb8f0): ASCII case-fold compare -----------------------
TEST(AnimObject, StrCmpNoCase) {
    CHECK_EQ(StrCmpNoCase("ABC", "abc"), 0);
    CHECK_EQ(StrCmpNoCase("Anim", "ANIM"), 0);
    CHECK(StrCmpNoCase("a", "b") < 0);
    CHECK(StrCmpNoCase("b", "a") > 0);
    CHECK_EQ(StrCmpNoCase("", ""), 0);
    // non-letters are NOT folded; '_' (0x5f) > 'Z'(0x5a) range untouched.
    CHECK(StrCmpNoCase("a_", "a_") == 0);
}

// --- AdvanceFrameIndex (0x5ccf18): the loop/ping/clamp state machine ---------
TEST(AnimObject, AdvanceFrameIndexNormal) {
    // flags=0: advance while cur<hiStop, else wrap to loStop.
    CHECK_EQ(AdvanceFrameIndex(0, 2, 5, 0, 6), 3);   // cur<hiStop -> cur+1
    CHECK_EQ(AdvanceFrameIndex(0, 5, 5, 0, 6), 0);   // cur==hiStop, no 0x10/1 -> loStop
}
TEST(AnimObject, AdvanceFrameIndexPing) {
    // flags&2 (ping reverse): cur<=loStop -> loStop+1 else cur-1.
    CHECK_EQ(AdvanceFrameIndex(2, 4, 5, 0, 6), 3);   // cur-1
    CHECK_EQ(AdvanceFrameIndex(2, 0, 5, 0, 6), 1);   // cur<=loStop -> loStop+1
}
TEST(AnimObject, AdvanceFrameIndexClamp) {
    // flags&0x10 (clamp to total-1): at end stay at total-1.
    CHECK_EQ(AdvanceFrameIndex(0x10, 5, 5, 0, 6), 6 - 1);  // v8>total-1 -> total-1
    CHECK_EQ(AdvanceFrameIndex(0x10, 3, 5, 0, 6), 4);      // cur<hiStop -> cur+1
    // flags&1 (bounce): at end -> cur-1.
    CHECK_EQ(AdvanceFrameIndex(1, 5, 5, 0, 6), 4);
}

// --- FindFreeMeshSlot (0x5cf114): empty list -> null, then match ------------
TEST(AnimObject, FindFreeMeshSlot) {
    StockGuard g;
    SetAnimObjHooks(nullptr);
    CHECK(FindFreeMeshSlot("animations/walk.baf") == nullptr);  // empty list
    AnimStream* s = MakeStream("animations/Walk.baf", 1, 4);
    AnimStock().push_back(s);
    // case-insensitive match.
    CHECK(FindFreeMeshSlot("animations/walk.baf") == s);
    CHECK(FindFreeMeshSlot("animations/run.baf") == nullptr);
}

// --- LoadStreamToStock (0x5d3858): dedup + prepend --------------------------
TEST(AnimObject, LoadStreamToStockDedup) {
    StockGuard g;
    static int loadCalls = 0; loadCalls = 0;
    AnimObjHooks h;
    h.loadBinaryAnimation = [](const char* path, guild::u8) -> AnimStream* {
        ++loadCalls;
        auto* s = new AnimStream(); s->name = path; s->meshType = 7; return s;
    };
    SetAnimObjHooks(&h);
    AnimStream* a = LoadStreamToStock("walk.baf", 0);
    CHECK(a != nullptr);
    CHECK_EQ(a->name, std::string("animations/walk.baf"));  // "animations/" prefix
    CHECK_EQ(loadCalls, 1);
    // second load of same name => dedup, no new load.
    AnimStream* b = LoadStreamToStock("WALK.baf", 0);  // case-insensitive dedup
    CHECK(b == a);
    CHECK_EQ(loadCalls, 1);
    SetAnimObjHooks(nullptr);
}

// --- CreateObjectAnim (0x5cef14): class gate, *3 fixup, pose snapshot --------
TEST(AnimObject, CreateObjectAnimClassGate) {
    SetAnimObjHooks(nullptr);
    AnimNode n;
    n.classByte = 2;  // not 3/4 -> no record allocated, node+464 set to 0.
    std::vector<guild::i32> frames = {10, 20, 30};
    ObjAnimRecord* r = CreateObjectAnim(&n, 0xAB, 3, frames, false);
    CHECK(r == nullptr);
    CHECK(n.objAnim == nullptr);
}
TEST(AnimObject, CreateObjectAnimFrameFixup) {
    AnimObjHooks h; static int tanCalls = 0; tanCalls = 0;
    h.buildFrameTangents = [](ObjAnimRecord*) { ++tanCalls; };
    h.frameCounter = 42;
    SetAnimObjHooks(&h);
    AnimNode n;
    n.classByte = 4;                       // createable
    n.pos[0] = 5; n.pos[1] = 6; n.pos[2] = 7;
    n.rot[0] = 1; n.rot[1] = 2; n.rot[2] = 3;
    std::vector<guild::i32> frames = {10, 20, 99};  // last gets prev-frame fixup
    ObjAnimRecord* r = CreateObjectAnim(&n, 0xAB, 3, frames, false);
    CHECK(r != nullptr);
    CHECK_EQ(r->frameCount, 3);
    CHECK_EQ(r->src, 0xAB);
    // last frame[0] = prev frame[0] BEFORE *3, then all *3: {30, 60, 60}.
    CHECK_EQ(r->frames[0], 30);
    CHECK_EQ(r->frames[1], 60);
    CHECK_EQ(r->frames[2], 60);
    // pose snapshot.
    CHECK_EQ(r->pos[0], 5); CHECK_EQ(r->rot[2], 3);
    // non-looped frame index.
    CHECK_EQ(r->curFrame, 0);
    CHECK_EQ(r->nextFrame, 1);
    CHECK_EQ(r->scale, 100);
    CHECK_EQ(n.frameStamp, 42);            // dword_62EB38 stamp
    CHECK_EQ(tanCalls, 1);
    FreeObjAnimData(&n);
    CHECK(n.objAnim == nullptr);
    SetAnimObjHooks(nullptr);
}

// --- AttachToBone (0x5d0b64) + Prune (0x5d0d38) -----------------------------
TEST(AnimObject, AttachToBoneAndPrune) {
    StockGuard g;
    AnimObjHooks h; static int asmCalls = 0; asmCalls = 0;
    h.assignSubMeshBones = [](guild::i32) { ++asmCalls; };
    h.computeBoneMatrices = [](guild::i32) {};
    h.frameCounter = 9; h.animEpoch = 3;
    SetAnimObjHooks(&h);

    AnimStream* s = MakeStream("animations/wave.baf", 5, 4);
    AnimStock().push_back(s);

    AnimNode model;
    model.meshType = 5;            // must match stream meshType
    model.drawData = 1234;

    i32 slot = AttachToBone(&model, "animations/wave.baf", 0);
    CHECK_EQ(slot, 0);
    CHECK(model.bone[0].used);
    CHECK(model.boneActive);
    CHECK_EQ(s->refCount, 1);
    CHECK_EQ(model.bone[0].weight, 100);
    CHECK_EQ(model.bone[0].markFrame, -1);
    CHECK(asmCalls >= 1);

    // mesh-type mismatch -> reject.
    AnimNode bad; bad.meshType = 99;
    CHECK_EQ(AttachToBone(&bad, "animations/wave.baf", 0), -1);

    // prune drops the matching channel + decrements refcount + clears boneActive.
    PruneExpiredAttachments(&model, "animations/wave.baf");
    CHECK(!model.bone[0].used);
    CHECK(!model.boneActive);
    CHECK_EQ(s->refCount, 0);
    SetAnimObjHooks(nullptr);
}

// --- AttachToBone fills all 3 channels then rejects -------------------------
TEST(AnimObject, AttachToBoneSlotLimit) {
    StockGuard g;
    SetAnimObjHooks(nullptr);
    AnimStream* s = MakeStream("animations/x.baf", 2, 2);
    AnimStock().push_back(s);
    AnimNode m; m.meshType = 2;
    CHECK_EQ(AttachToBone(&m, "animations/x.baf", 0), 0);
    CHECK_EQ(AttachToBone(&m, "animations/x.baf", 0), 1);
    CHECK_EQ(AttachToBone(&m, "animations/x.baf", 0), 2);
    CHECK_EQ(AttachToBone(&m, "animations/x.baf", 0), -1);  // full
    CHECK_EQ(s->refCount, 3);
}
