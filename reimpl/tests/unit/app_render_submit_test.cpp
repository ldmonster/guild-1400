// Unit tests for the per-frame render-submit slot builders (guild::app):
//   VIBE_GameLogic_Objects  @0x412fa0  -> app::GameLogicObjects
//   VIBE_GameLogic_Entities @0x413580  -> app::GameLogicEntities
//
// The cross-module sibling leaves are injected recording hooks; the entity array
// + widget slots use the real gui::g_widgets / Widget_AllocSlot path. We assert
// the bounds/alloc gates, the per-kind dispatch field writes, and the
// state-update / decompress / animation / finalize flow of the two functions.
#include "test.h"

#include "app/render_submit.h"
#include "gui/object.h"

#include <vector>

using namespace guild;
using guild::app::EntitySubmitRecord;
using guild::app::RenderSubmitHooks;
using guild::app::GameLogicObjects;
using guild::app::GameLogicEntities;

namespace {

// A recording hook block so each test can see which leaves fired.
struct Rec {
    std::vector<int> resolved, marked, animFrames, finalized;
    int shapeAnimReturns = 4242;
    int coordReturns = 1;
    int stateUpdateReturns = 77;
    int decompressReturns = 1;     // success by default
    int finalizeReturns = 9;
    int blobReturns = 555;
    // metric table: word @ off (caller seeds it)
    int metric[256] = {0};

    RenderSubmitHooks make() {
        RenderSubmitHooks h{};
        h.resolveObjectState = [](int idx, int* outState, int* outIndex) -> int {
            *outState = 1000 + idx; *outIndex = idx; return 0;
        };
        h.markObjectUsed = [](int){};
        h.shapeAnimRegisterSlot = [](int, int, int) -> int { return 4242; };
        h.coordTransform = [](int, int) -> int { return 1; };
        h.gridMetricWord = [](int, int off) -> int { return 100 + off; };
        h.slotGeometry = [](int) -> int { return 1; };
        h.stateUpdate = [](int) -> int { return 77; };
        h.decompressBlob = [](int, int) -> int { return 1; };
        h.animationBasic = [](int, int, int, int, int){};
        h.stateGetCurrent = [](int, int, int, int){};
        h.decompressionFinalize = [](int) -> int { return 9; };
        // stateBlob left null: the builder then reads entities[blobIdx].kind —
        // the binary's *(dword_62D204 + 84*blobIdx + 60) dword (0x4136a4), which
        // is ALSO the per-kind dispatch selector (0x4136b3 cmp edx, 5).
        h.stateBlob = nullptr;
        h.gridOffset = 0;
        h.clipX0 = 1; h.clipX1 = 2; h.clipY0 = 3; h.clipY1 = 4;
        return h;
    }
};

} // namespace

// ---- GameLogicObjects: bounds + alloc gates --------------------------------
TEST(AppRenderSubmit, Objects_OutOfRangeReturnsMinus1) {
    gui::ResetWidgets();
    std::vector<EntitySubmitRecord> ents(4);
    Rec rec; auto h = rec.make();
    // entityIdx > count -> -1 (no slot allocated).
    int r = GameLogicObjects(10, 20, /*entityIdx=*/100, ents.data(), 3, h);
    CHECK_EQ(r, -1);
}

// ---- GameLogicObjects: kind-1 fills w/h from geom metric -------------------
TEST(AppRenderSubmit, Objects_Kind1FillsSizeFromMetric) {
    gui::ResetWidgets();
    std::vector<EntitySubmitRecord> ents(8);
    ents[2].kind = 1;            // +60 kind byte
    Rec rec; auto h = rec.make();
    int slot = GameLogicObjects(11, 22, /*entityIdx=*/2, ents.data(), 7, h);
    CHECK(slot >= 0);
    gui::Widget& w = gui::g_widgets[slot];
    CHECK_EQ((int)w.at<u8>(24), 1);             // kind byte copied
    CHECK_EQ((int)w.at<i16>(16), 11);           // x
    CHECK_EQ((int)w.at<i16>(18), 22);           // y
    CHECK_EQ((int)w.at<i16>(26), 2);            // order = 2
    // kind 1 -> w(+20)=metric(44)=144, h(+22)=metric(46)=146 (gridMetricWord)
    CHECK_EQ((int)w.at<i16>(20), 144);
    CHECK_EQ((int)w.at<i16>(22), 146);
    // clip bounds
    CHECK_EQ((int)w.at<i16>(28), 1);
    CHECK_EQ((int)w.at<i16>(34), 4);
}

// ---- GameLogicObjects: kind-4 registers a shape-anim slot ------------------
TEST(AppRenderSubmit, Objects_Kind4RegistersShapeAnim) {
    gui::ResetWidgets();
    std::vector<EntitySubmitRecord> ents(8);
    ents[1].kind = 4;
    Rec rec; auto h = rec.make();
    int slot = GameLogicObjects(0, 0, 1, ents.data(), 7, h);
    CHECK(slot >= 0);
    gui::Widget& w = gui::g_widgets[slot];
    CHECK_EQ((int)w.at<u8>(24), 4);
    CHECK_EQ(w.at<i32>(116), 4242);             // shapeAnimRegisterSlot handle
}

// ---- GameLogicObjects: kind-8 grid placement (+72 button, +104/105) -------
TEST(AppRenderSubmit, Objects_Kind8GridPlacement) {
    gui::ResetWidgets();
    std::vector<EntitySubmitRecord> ents(8);
    ents[3].kind = 8;
    Rec rec; auto h = rec.make();
    int slot = GameLogicObjects(5, 6, 3, ents.data(), 7, h);
    CHECK(slot >= 0);
    gui::Widget& w = gui::g_widgets[slot];
    CHECK_EQ((int)w.at<u8>(24), 8);
    CHECK_EQ(w.at<i32>(72), 1);                 // kind-8 button flag
    CHECK_EQ((int)w.at<u8>(104), 8);            // grid metric bytes
    CHECK_EQ((int)w.at<u8>(105), 8);
    CHECK_EQ(w.at<i32>(456), -1);
    CHECK_EQ(w.at<i32>(460), -1);
}

// ---- GameLogicObjects: kind-17 sets +116=2 --------------------------------
TEST(AppRenderSubmit, Objects_Kind17) {
    gui::ResetWidgets();
    std::vector<EntitySubmitRecord> ents(8);
    ents[0].kind = 17;
    Rec rec; auto h = rec.make();
    int slot = GameLogicObjects(0, 0, 0, ents.data(), 7, h);
    CHECK(slot >= 0);
    gui::Widget& w = gui::g_widgets[slot];
    CHECK_EQ(w.at<i32>(116), 2);
    CHECK_EQ((int)w.at<i16>(20), 112);          // metric(12)
    CHECK_EQ((int)w.at<i16>(22), 114);          // metric(14)
}

// ---- GameLogicEntities: success flow -> finalize result -------------------
TEST(AppRenderSubmit, Entities_SuccessFlow) {
    std::vector<EntitySubmitRecord> ents(8);
    ents[2].kind = 1;            // a kind-1 (basic) record
    ents[2].stateHandle = 0;     // unrealised -> State_Update fires
    Rec rec; auto h = rec.make();
    int animCount = 0, finalizeCount = 0;
    static int s_anim = 0, s_final = 0;
    s_anim = 0; s_final = 0;
    h.animationBasic = [](int, int, int, int, int) { ++s_anim; };
    h.decompressionFinalize = [](int) -> int { ++s_final; return 9; };
    int r = GameLogicEntities(3, 4, /*entityIdx=*/2, /*out=*/123, ents.data(), 7, h);
    (void)animCount; (void)finalizeCount;
    CHECK_EQ(r, 9);              // returns Decompression_Finalize result
    CHECK_EQ(s_anim, 1);        // kind 1 -> Animation_Basic(...,0)
    CHECK_EQ(s_final, 1);
}

// ---- GameLogicEntities: decompress failure short-circuits -----------------
TEST(AppRenderSubmit, Entities_DecompressFailReturnsZero) {
    std::vector<EntitySubmitRecord> ents(8);
    ents[1].kind = 1;
    Rec rec; auto h = rec.make();
    h.decompressBlob = [](int, int) -> int { return 0; }; // failure
    static int s_final2 = 0; s_final2 = 0;
    h.decompressionFinalize = [](int) -> int { ++s_final2; return 9; };
    int r = GameLogicEntities(0, 0, 1, 123, ents.data(), 7, h);
    CHECK_EQ(r, 0);
    CHECK_EQ(s_final2, 0);      // finalize NOT reached on a failed decode
}

// ---- GameLogicEntities: out-of-range short-circuits -----------------------
TEST(AppRenderSubmit, Entities_OutOfRange) {
    std::vector<EntitySubmitRecord> ents(4);
    Rec rec; auto h = rec.make();
    int r = GameLogicEntities(0, 0, /*entityIdx=*/99, 0, ents.data(), 3, h);
    CHECK_EQ(r, 0);
}

// ---- GameLogicEntities: kind-5 redirect path (gilde.exe 0x413580) ----------
// Verifies against the binary:
//  * the dispatch selector is the +60 dword of the BLOB record (the pre-redirect
//    record, 0x4136a4 + 0x4136b3), so kind 5/8 pairs take the redirect branch;
//  * Animation_Basic's 3rd arg is the state handle ecx (State_Update result,
//    0x413614), and its 5th arg is the BYTE delta (u8)(v7 - idx)
//    (0x41377d..0x41378f: sub al, ah; and eax, 0FFh);
//  * State_GetCurrent receives arg3 = *(int*)(rec+0x50)>>16 (the i16 at +82) and
//    arg4 = *(int*)(rec+0x4E)>>16 (the i16 at +80) of the POST-redirect record
//    (0x413700/0x413703 + sar 16).
TEST(AppRenderSubmit, Entities_RedirectByteDeltaAndGetCurrentArgs) {
    std::vector<EntitySubmitRecord> ents(8);
    ents[5].kind = 5;          // linked pair -> redirect
    ents[5].suppress = 0;
    ents[5].linkIndex = 1;     // partner record
    ents[1].stateHandle = 0;   // unrealised -> State_Update(5) fires
    ents[1].subStateLo = 111;  // i16 at +80 -> State_GetCurrent arg4
    ents[1].subStateHi = 222;  // i16 at +82 -> State_GetCurrent arg3

    Rec rec; auto h = rec.make();
    static int s_animFrame, s_animArg, s_gcW, s_gcH;
    s_animFrame = s_animArg = s_gcW = s_gcH = -999;
    h.stateUpdate = [](int) -> int { return 77; };
    h.animationBasic = [](int, int, int frame, int, int arg) {
        s_animFrame = frame; s_animArg = arg;
    };
    h.stateGetCurrent = [](int, int, int w, int hh) { s_gcW = w; s_gcH = hh; };

    int r = GameLogicEntities(3, 4, /*entityIdx=*/5, /*out=*/123, ents.data(), 7, h);
    CHECK_EQ(r, 9);            // Decompression_Finalize result
    CHECK_EQ(s_animFrame, 77); // ecx = State_Update result (0x413614)
    CHECK_EQ(s_animArg, 4);    // (u8)(v7 - idx) = (u8)(5 - 1) = 4
    CHECK_EQ(s_gcW, 222);      // arg3 = i16 at +82 of the partner record
    CHECK_EQ(s_gcH, 111);      // arg4 = i16 at +80 of the partner record
}
