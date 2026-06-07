// Unit tests for the interaction3 orientation & move-target slice (VIBE_Interaction_*
// 0x46bxxx..0x470bxx). Each non-trivial function is covered against the recovered
// semantics; deterministic golden values are computed by hand (see the comments).
#include "test.h"

#include "sim/interaction3.h"

using namespace guild;
using namespace guild::sim;

namespace {

// ---- shared hook fixtures -------------------------------------------------
ActionObject g_obj;          // QueryFind return
int  g_spin = 0;             // spin direction UseObjectAction writes
bool g_hasSlot = true;
int  g_useResult = 1;        // UseObjectAction result (1 == proceed)
int  g_useCalls = 0;
float g_base = 4.0f;
float g_quad[4] = {1.0f, 2.0f, 3.0f, 4.0f};

ActionObject HookQueryFind(int, int) { return g_obj; }
bool         HookHasSlot(int)        { return g_hasSlot; }
int          HookUse(int, const ActionEvent*, int* spinDir) {
    ++g_useCalls; *spinDir = g_spin; return g_useResult;
}
float        HookBase(int)           { return g_base; }
float        HookQuad(int col, int)  { return g_quad[col & 3]; }

void WireOrient() {
    ResetInteraction3Hooks();
    ResetOrientOutput();
    g_i3Hooks.queryFind = &HookQueryFind;
    g_i3Hooks.hasInventorySlot = &HookHasSlot;
    g_i3Hooks.useObjectAction = &HookUse;
    g_i3Hooks.baseSpin = &HookBase;
    g_i3Hooks.quadColumn = &HookQuad;
    g_obj = ActionObject{};
    g_obj.present = true;
    g_obj.scale = 3.0f;
    g_spin = 0; g_hasSlot = true; g_useResult = 1; g_useCalls = 0;
    g_base = 4.0f;
}

ActionEvent MakeEvent() {
    ActionEvent e;
    e.actorNodeKey = 1; e.targetNodeId = 2; e.directionRow = 0;
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Scalar orient: success/spin paths and early-outs.
// ---------------------------------------------------------------------------
TEST(Interaction3Orient, ScalarSpinPositiveWritesBaseTimesScale) {
    WireOrient();
    g_spin = 1;                       // base 4 * scale 3 = 12
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget27(0, &e), 27);
    CHECK_EQ(g_orientOut.s[0], 12.0f);
}

TEST(Interaction3Orient, ScalarSpinNegativeAppliesHalfFactor) {
    WireOrient();
    g_spin = -1;                      // -(4*3)*0.5 = -6
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget28(0, &e), 28);
    CHECK_EQ(g_orientOut.s[0], -6.0f);
}

TEST(Interaction3Orient, Action30And31OmitHalfFactor) {
    WireOrient();
    g_spin = -1;                      // -(4*3) = -12 (no half)
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget30(0, &e), 30);
    CHECK_EQ(g_orientOut.s[0], -12.0f);

    ResetOrientOutput();
    CHECK_EQ(OrientToActionTarget31(0, &e), 31);
    CHECK_EQ(g_orientOut.s[0], -12.0f);
}

TEST(Interaction3Orient, ScalarSpinZeroLeavesOutputUnchanged) {
    WireOrient();
    g_spin = 0;
    g_orientOut.s[0] = 99.0f;          // pre-seed, must stay
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget29(0, &e), 29);
    CHECK_EQ(g_orientOut.s[0], 99.0f);
}

TEST(Interaction3Orient, MissingTargetReturnsZeroAndSkipsAction) {
    WireOrient();
    g_obj.present = false;
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget27(0, &e), 0);
    CHECK_EQ(g_useCalls, 0);           // UseObjectAction never reached
}

TEST(Interaction3Orient, NoInventorySlotReturnsZero) {
    WireOrient();
    g_hasSlot = false;
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget32(0, &e), 0);
    CHECK_EQ(g_useCalls, 0);
}

TEST(Interaction3Orient, UseActionFailureReturnsZero) {
    WireOrient();
    g_useResult = 0;                   // != 1 -> early out
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientToActionTarget34(0, &e), 0);
    CHECK_EQ(g_useCalls, 1);           // reached UseObjectAction, then bailed
    CHECK_EQ(g_orientOut.s[0], 0.0f);
}

// ---------------------------------------------------------------------------
// Quad orient.
// ---------------------------------------------------------------------------
TEST(Interaction3Quad, Quad33PositiveWritesFourEqualLanes) {
    WireOrient();
    g_spin = 1;                        // all four = base 4 * scale 3 = 12
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientQuadToTarget33(0, &e), 33);
    for (int k = 0; k < 4; ++k) CHECK_EQ(g_orientOut.s[k], 12.0f);
}

TEST(Interaction3Quad, Quad33NegativeHalves) {
    WireOrient();
    g_spin = -1;                       // -(4*3*0.5) = -6
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientQuadToTarget33(0, &e), 33);
    for (int k = 0; k < 4; ++k) CHECK_EQ(g_orientOut.s[k], -6.0f);
}

TEST(Interaction3Quad, Quad35UsesDirectionTableColumns) {
    WireOrient();
    g_spin = 1;                        // col[k]*scale: {1,2,3,4} * 3 = {3,6,9,12}
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientQuadToTarget35(0, &e), 35);
    CHECK_EQ(g_orientOut.s[0], 3.0f);
    CHECK_EQ(g_orientOut.s[1], 6.0f);
    CHECK_EQ(g_orientOut.s[2], 9.0f);
    CHECK_EQ(g_orientOut.s[3], 12.0f);
}

TEST(Interaction3Quad, Quad35NegativeHalvesEachColumn) {
    WireOrient();
    g_spin = -1;                       // -(col*scale*0.5): -(1*3*.5)=-1.5 etc.
    ActionEvent e = MakeEvent();
    CHECK_EQ(OrientQuadToTarget35(0, &e), 35);
    CHECK_EQ(g_orientOut.s[0], -1.5f);
    CHECK_EQ(g_orientOut.s[1], -3.0f);
    CHECK_EQ(g_orientOut.s[2], -4.5f);
    CHECK_EQ(g_orientOut.s[3], -6.0f);
}

// ---------------------------------------------------------------------------
// ComputeApproachOffset.
// ---------------------------------------------------------------------------
namespace {
int   g_scorerResult = 7;
float g_scorerX = 1.0f, g_scorerY = 2.0f;
int   g_lawId = -999;       // captures the lawId the distance scorer was called with
int   g_relRecord = -999;   // captures the record the weighted scorer saw
bool  g_factorTen = false;
bool  g_forceWeighted = false;
int   g_lawActive = 0;

int HookWeighted(float* x, float* y, int rec, char, int, int, int) {
    g_relRecord = rec; *x = g_scorerX; *y = g_scorerY; return g_scorerResult;
}
int HookDistance(float* x, float* y, char, int lawId) {
    g_lawId = lawId; *x = g_scorerX; *y = g_scorerY; return g_scorerResult;
}
bool HookFactorTen(int) { return g_factorTen; }
bool HookForceWeighted(int) { return g_forceWeighted; }
int  HookLawActive(int) { return g_lawActive; }

void WireScorers() {
    ResetInteraction3Hooks();
    g_i3Hooks.relationWeighted = &HookWeighted;
    g_i3Hooks.relationDistance = &HookDistance;
    g_i3Hooks.approachFactorIsTen = &HookFactorTen;
    g_i3Hooks.objectForcesWeighted = &HookForceWeighted;
    g_i3Hooks.lawRecordActive = &HookLawActive;
    g_lawId = -999; g_relRecord = -999;
    g_scorerResult = 7; g_scorerX = 1.0f; g_scorerY = 2.0f;
    g_factorTen = false; g_forceWeighted = false; g_lawActive = 0;
}
} // namespace

TEST(Interaction3Approach, SkipWritesFarSentinelAndReturnsZero) {
    WireScorers();
    float x = 0, y = 0;
    CHECK_EQ(ComputeApproachOffset(&x, &y, 100, 0, /*skip=*/1, 0, 0), 0);
    CHECK_EQ(x, -1.0e30f);
    CHECK_EQ(y, -1.0e30f);
}

TEST(Interaction3Approach, FactorOneLeavesScorerVector) {
    WireScorers();
    g_factorTen = false;
    float x = 0, y = 0;
    CHECK_EQ(ComputeApproachOffset(&x, &y, 100, 0, /*skip=*/0, 0, 0), 1);
    CHECK_EQ(x, 1.0f);
    CHECK_EQ(y, 2.0f);
}

TEST(Interaction3Approach, FactorTenScalesScorerVector) {
    WireScorers();
    g_factorTen = true;
    float x = 0, y = 0;
    CHECK_EQ(ComputeApproachOffset(&x, &y, 100, 0, /*skip=*/0, 0, 0), 1);
    CHECK_EQ(x, 10.0f);
    CHECK_EQ(y, 20.0f);
}

// ---------------------------------------------------------------------------
// ComputeMoveTarget* — branch selection (weighted vs distance) and law ids.
// ---------------------------------------------------------------------------
TEST(Interaction3MoveTarget, AInactiveLawUsesWeighted) {
    WireScorers();
    g_lawActive = 0;                   // inactive -> weighted
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetA(&x, &y, 555, 0, 0, 0), 7);
    CHECK_EQ(g_relRecord, 555);        // weighted scorer saw the record
    CHECK_EQ(g_lawId, -999);           // distance scorer NOT called
}

TEST(Interaction3MoveTarget, AActiveLawUsesDistanceWithLaw23) {
    WireScorers();
    g_lawActive = 1;                   // active -> distance (law 23)
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetA(&x, &y, 555, 0, 0, 0), 7);
    CHECK_EQ(g_lawId, 23);
    CHECK_EQ(g_relRecord, -999);       // weighted NOT called
}

TEST(Interaction3MoveTarget, AObjectClauseForcesWeightedEvenWhenLawActive) {
    WireScorers();
    g_lawActive = 1;
    g_forceWeighted = true;            // object class==5&flag -> weighted overrides
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetA(&x, &y, 7, 0, 0, 0), 7);
    CHECK_EQ(g_relRecord, 7);
    CHECK_EQ(g_lawId, -999);
}

TEST(Interaction3MoveTarget, BUsesLaw20OnActiveDistance) {
    WireScorers();
    g_lawActive = 1;
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetB(&x, &y, 1, 0, 0, 0), 7);
    CHECK_EQ(g_lawId, 20);
}

TEST(Interaction3MoveTarget, SocialOutOfRangeReturnsZeroAndZeroesOutput) {
    WireScorers();
    float x = 5, y = 5;
    CHECK_EQ(ComputeMoveTargetSocial(&x, &y, 1, 0, /*actionCode=*/27, 0, 0), 0);
    CHECK_EQ(x, 0.0f);
    CHECK_EQ(y, 0.0f);
    // 35 is also out of range (range is 28..34)
    CHECK_EQ(ComputeMoveTargetSocial(&x, &y, 1, 0, 35, 0, 0), 0);
}

TEST(Interaction3MoveTarget, SocialInRangeActiveLawUsesDistanceLaw16) {
    WireScorers();
    g_lawActive = 1;
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetSocial(&x, &y, 1, 0, 30, 0, 0), 7);
    CHECK_EQ(g_lawId, 16);
}

TEST(Interaction3MoveTarget, SocialInRangeInactiveLawUsesWeighted) {
    WireScorers();
    g_lawActive = 0;
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetSocial(&x, &y, 42, 0, 28, 0, 0), 7);
    CHECK_EQ(g_relRecord, 42);
}

TEST(Interaction3MoveTarget, SocialAltUsesLaw17) {
    WireScorers();
    g_lawActive = 1;
    float x = 0, y = 0;
    CHECK_EQ(ComputeMoveTargetSocialAlt(&x, &y, 1, 0, 34, 0, 0), 7);
    CHECK_EQ(g_lawId, 17);
}

// ---------------------------------------------------------------------------
// EvalRestSlotFree.
// ---------------------------------------------------------------------------
namespace {
int   g_method = 5;
// Catalog model: a column "completes" (free) when its byte is 0 AND float >= 0 for
// at least 4 steps. We drive completion via simple per-column toggles.
int  g_byteStopAt[16];   // step index at which the byte becomes 0 (always-0 if <0)
                         // simpler: define explicit tables below
u8   g_colByte[16][8];
float g_colFloat[16][8];

int HookSelect(int, int) { return g_method; }
u8  HookByte(int, int col, int k)  { return g_colByte[col & 15][k & 7]; }
float HookFloat(int, int col, int k){ return g_colFloat[col & 15][k & 7]; }

void WireRest() {
    ResetInteraction3Hooks();
    g_i3Hooks.selectBestMethod = &HookSelect;
    g_i3Hooks.methodCatalogByte = &HookByte;
    g_i3Hooks.methodCatalogFloat = &HookFloat;
    g_method = 5;
    (void)g_byteStopAt;
    for (int c = 0; c < 16; ++c)
        for (int k = 0; k < 8; ++k) { g_colByte[c][k] = 1; g_colFloat[c][k] = 1.0f; }
}
} // namespace

TEST(Interaction3Rest, ArmedReturnsZero) {
    WireRest();
    CHECK_EQ(EvalRestSlotFree(0, nullptr, /*armed=*/1, nullptr), 0);
}

TEST(Interaction3Rest, NoMethodReturns26) {
    WireRest();
    g_method = 0;
    CHECK_EQ(EvalRestSlotFree(0, nullptr, 0, nullptr), 26);
}

TEST(Interaction3Rest, BothScansCompleteEmitsMethodAndVectors) {
    WireRest();
    // Both columns 6 and 14 hold their loop condition for >=4 steps (byte 1).
    float a[8] = {0}, b[8] = {0};
    CHECK_EQ(EvalRestSlotFree(0, a, 0, b), 5);   // returns the method id
    CHECK_EQ(a[2], 1.0f);                         // scratch vector marker
    CHECK_EQ(b[2], 1.0f);
}

TEST(Interaction3Rest, FirstScanStopsEarlyReturns26) {
    WireRest();
    // Column 6 condition fails at step 2 (byte 0 and float negative) -> early stop.
    g_colByte[6][2] = 0; g_colFloat[6][2] = -1.0f;
    float a[8] = {0}, b[8] = {0};
    CHECK_EQ(EvalRestSlotFree(0, a, 0, b), 26);
    CHECK_EQ(a[2], 0.0f);                         // vectors NOT emitted
}

TEST(Interaction3Rest, SecondScanStopsEarlyReturns26) {
    WireRest();
    // Column 6 completes (all 1), column 14 fails at step 1.
    g_colByte[14][1] = 0; g_colFloat[14][1] = -2.0f;
    float a[8] = {0}, b[8] = {0};
    CHECK_EQ(EvalRestSlotFree(0, a, 0, b), 26);
    CHECK_EQ(b[2], 0.0f);
}
