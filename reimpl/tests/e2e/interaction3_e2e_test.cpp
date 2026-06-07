// E2E flow for the interaction3 slice: drive a full "social action orientation"
// sequence the way the per-action evaluator would — pick a move target, then run the
// object-orient spin — across several of the translated functions with one coherent
// set of installed hooks (all inert defaults overridden once).
#include "test.h"

#include "sim/interaction3.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A tiny simulated world the hooks read.
struct World {
    bool   targetExists = true;
    float  targetScale  = 2.0f;
    int    spin         = 1;        // object-action spin result
    int    lawActive    = 0;        // Gesetz law-active flag
    float  baseMag      = 5.0f;
    int    useCalls     = 0;
};
World g_w;

ActionObject FQuery(int, int) {
    ActionObject o; o.present = g_w.targetExists; o.scale = g_w.targetScale; o.itemId = 7;
    return o;
}
bool  FSlot(int)            { return true; }
int   FUse(int, const ActionEvent*, int* d) { ++g_w.useCalls; *d = g_w.spin; return 1; }
int   FLaw(int)            { return g_w.lawActive; }
float FBase(int)           { return g_w.baseMag; }
int   FWeighted(float* x, float* y, int, char, int, int, int) { *x = 3.0f; *y = 4.0f; return 11; }
int   FDistance(float* x, float* y, char, int) { *x = 1.0f; *y = 1.0f; return 22; }

void Wire() {
    ResetInteraction3Hooks();
    ResetOrientOutput();
    g_i3Hooks.queryFind = &FQuery;
    g_i3Hooks.hasInventorySlot = &FSlot;
    g_i3Hooks.useObjectAction = &FUse;
    g_i3Hooks.lawRecordActive = &FLaw;
    g_i3Hooks.baseSpin = &FBase;
    g_i3Hooks.relationWeighted = &FWeighted;
    g_i3Hooks.relationDistance = &FDistance;
    g_w = World{};
}

} // namespace

// Full social-orient flow: choose a move-target vector (inactive law -> weighted),
// then perform the object orient spin. Asserts the cross-function data flow.
TEST(Interaction3E2E, SocialActionPickThenOrient) {
    Wire();
    g_w.lawActive = 0;             // -> weighted move target (3,4)
    g_w.spin = 1;                  // positive spin: s0 = base 5 * scale 2 = 10

    float mx = 0, my = 0;
    // Action code 30 is in the social range 28..34.
    CHECK_EQ(ComputeMoveTargetSocial(&mx, &my, /*record=*/100, 0, /*code=*/30, 0, 0), 11);
    CHECK_EQ(mx, 3.0f);
    CHECK_EQ(my, 4.0f);

    ActionEvent e; e.actorNodeKey = 1; e.targetNodeId = 2; e.directionRow = 0;
    CHECK_EQ(OrientToActionTarget30(0, &e), 30);
    CHECK_EQ(g_orientOut.s[0], 10.0f);   // 5 * 2
    CHECK_EQ(g_w.useCalls, 1);
}

// Same flow with the law active flips the move target onto the distance scorer, and
// a negative spin halves the orient output (action 27 uses the half factor).
TEST(Interaction3E2E, ActiveLawDistanceThenNegativeSpinHalf) {
    Wire();
    g_w.lawActive = 1;             // -> distance move target (1,1)
    g_w.spin = -1;                 // negative spin, half factor: -(5*2)*0.5 = -5

    float mx = 0, my = 0;
    CHECK_EQ(ComputeMoveTargetSocialAlt(&mx, &my, 100, 0, 31, 0, 0), 22);
    CHECK_EQ(mx, 1.0f);
    CHECK_EQ(my, 1.0f);

    ActionEvent e; e.actorNodeKey = 1; e.targetNodeId = 2;
    CHECK_EQ(OrientToActionTarget27(0, &e), 27);
    CHECK_EQ(g_orientOut.s[0], -5.0f);
}

// A vanished target aborts the orient before the spin: move target still computes,
// but the orient bails with 0 and leaves the output untouched.
TEST(Interaction3E2E, VanishedTargetAbortsOrient) {
    Wire();
    g_w.lawActive = 0;
    float mx = 0, my = 0;
    CHECK_EQ(ComputeMoveTargetSocial(&mx, &my, 1, 0, 28, 0, 0), 11);

    g_w.targetExists = false;
    ActionEvent e; e.actorNodeKey = 1; e.targetNodeId = 9;
    CHECK_EQ(OrientToActionTarget29(0, &e), 0);
    CHECK_EQ(g_orientOut.s[0], 0.0f);
    CHECK_EQ(g_w.useCalls, 0);     // never reached the action
}
