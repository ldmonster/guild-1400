// gilde.exe 0x5f4428 VIBE_Shadow_ResetLightList + 0x5f43f4 VIBE_Render_PushToDrawList —
// the per-frame shadow-light collector (max 4 active type-8 light nodes). Golden, no assets.
#include "tests/framework/test.h"
#include "render/shadow_light_list.h"
#include <vector>
using namespace guild;
using namespace guild::render;

// --- ShadowPushLight (the collector callback) directly ---
TEST(ShadowLightList, PushFiltersAndCapsAtFour) {
    ShadowLightList L;
    SceneNode caster; caster.flags529 = kShadowCasterFlag;     // +529 bit2
    SceneNode plain;  plain.flags529 = 0;                      // not a caster
    // non-caster: not added, walk continues (room) -> 1
    CHECK_EQ((int)ShadowPushLight(L, &plain), 1);
    CHECK_EQ(L.count, 0);
    // four casters: added; the 4th returns 0 (stop the walk)
    CHECK_EQ((int)ShadowPushLight(L, &caster), 1); CHECK_EQ(L.count, 1);
    CHECK_EQ((int)ShadowPushLight(L, &caster), 1); CHECK_EQ(L.count, 2);
    CHECK_EQ((int)ShadowPushLight(L, &caster), 1); CHECK_EQ(L.count, 3);
    CHECK_EQ((int)ShadowPushLight(L, &caster), 0); CHECK_EQ(L.count, 4);
    for (int i = 0; i < 4; ++i) CHECK(L.lights[i] == &caster);
}

// --- ShadowResetLightList over a real scene-graph walk ---
TEST(ShadowLightList, ResetWalksAndCollectsCasters) {
    // Six type-8 light nodes as sibling-list heads; L1 is not a caster.
    SceneNode L[6], term;
    for (int i = 0; i < 6; ++i) { L[i].nodeType = 8; L[i].flags528 = 0x01; }
    term.nodeType = 8; term.flags528 = 0x01;
    for (int i = 0; i < 5; ++i) L[i].nextSibling = &L[i+1];
    L[5].nextSibling = &term;                     // chain ends at the terminator sentinel
    for (int i = 0; i < 6; ++i) L[i].flags529 = kShadowCasterFlag;
    L[1].flags529 = 0;                            // L1 not a caster -> skipped

    UniverseRoot root; root.childHead = &L[0];

    ShadowLightList list;
    // env.listTerminator is set inside ShadowResetLightList? No — it builds its own env with
    // root only; the chain must terminate via the terminator sentinel == nullptr-free. Use the
    // explicit-list variant: walk stops at the terminator we wire as the chain end.
    // (ShadowResetLightList builds env.root; the sentinel is the engine's dword_13FCF4C — here
    //  the chain's term node carries flags528 bit0 so the sibling walk halts after it.)
    ShadowResetLightList(list, &root);
    // Walk order visits L0(caster),L1(skip),L2,L3,L4 -> collects 4 then aborts; L5 unreached.
    CHECK_EQ(list.count, 4);
    CHECK(list.lights[0] == &L[0]);
    CHECK(list.lights[1] == &L[2]);
    CHECK(list.lights[2] == &L[3]);
    CHECK(list.lights[3] == &L[4]);
}

TEST(ShadowLightList, NullRootEmpty) {
    ShadowLightList list; list.count = 3;
    ShadowResetLightList(list, nullptr);
    CHECK_EQ(list.count, 0);
}

// --- the void() FrameHook binding (active universe) ---
TEST(ShadowLightList, ActiveThunkRebuilds) {
    SceneNode a, b, term;
    a.nodeType = 8; a.flags528 = 0x01; a.flags529 = kShadowCasterFlag; a.nextSibling = &b;
    b.nodeType = 8; b.flags528 = 0x01; b.flags529 = kShadowCasterFlag; b.nextSibling = &term;
    term.nodeType = 8; term.flags528 = 0x01;
    UniverseRoot root; root.childHead = &a;
    SetActiveShadowUniverse(&root);
    ShadowResetLightListActive();
    CHECK_EQ(CollectedShadowLights().count, 2);
    SetActiveShadowUniverse(nullptr);
    ShadowResetLightListActive();
    CHECK_EQ(CollectedShadowLights().count, 0);
}
