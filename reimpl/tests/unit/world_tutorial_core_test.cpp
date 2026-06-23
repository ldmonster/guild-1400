// Unit tests for src/world/tutorial.cpp — the tutorial step-PROGRESSION rules
// core (gilde.exe VIBE_Tutorial_* @0x596fa4+). These exercise the deterministic
// advance state machine, the form-resource selection bound, and the chain walk.
//
// Wave-12 hardening focus: out-of-range step indices, the form-position end
// sentinel / out-of-range form-position bytes, a null chapter, and a step count
// of 0 / many — none of these may index past kFormResource[4] or the step array.
#include "test.h"

#include "world/tutorial.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

TutorialStep MakeStep(u8 phase, u8 formPos, u32 textId = 100, u32 voiceId = 0) {
    TutorialStep s;
    std::memset(&s, 0, sizeof(s));
    s.id      = 1;
    s.phase   = phase;
    s.formPos = formPos;
    s.textId  = textId;
    s.voiceId = voiceId;
    return s;
}

TutorialState MakeState(TutorialChapter* ch, int stepIndex, u8 lastPhase) {
    TutorialState st;
    std::memset(&st, 0, sizeof(st));
    st.active    = true;
    st.chapter   = ch;
    st.stepIndex = stepIndex;
    st.lastPhase = lastPhase;
    st.formOpen  = false;
    return st;
}

} // namespace

// --- FormResource: every valid position resolves; the sentinel / OOB -> null ---
TEST(WorldTutorialCore, FormResourceBoundsAllPositions) {
    CHECK(TutorialFormResource(kTutPosLeft)   != nullptr);
    CHECK(TutorialFormResource(kTutPosRight)  != nullptr);
    CHECK(TutorialFormResource(kTutPosTop)    != nullptr);
    CHECK(TutorialFormResource(kTutPosBottom) != nullptr);
    CHECK(std::strcmp(TutorialFormResource(0), "tutorial\\left_form") == 0);
    CHECK(std::strcmp(TutorialFormResource(3), "tutorial\\bottom_form") == 0);
}

TEST(WorldTutorialCore, FormResourceEndSentinelAndOutOfRange) {
    CHECK(TutorialFormResource(kTutorialPosEnd) == nullptr);  // == 4 -> end
    CHECK(TutorialFormResource(5)   == nullptr);
    CHECK(TutorialFormResource(255) == nullptr);              // would OOB kFormResource[4]
}

// --- IsInactive / SetActiveChapter -------------------------------------------
TEST(WorldTutorialCore, InactiveGate) {
    TutorialState st = MakeState(nullptr, 0, 0);
    st.active = false;
    CHECK(TutorialIsInactive(st));
    CHECK_EQ(TutorialSetActiveChapter(st, nullptr), -4);    // inactive -> -4
    st.active = true;
    CHECK(!TutorialIsInactive(st));
    TutorialChapter ch{}; ch.stepCount = 1;
    CHECK_EQ(TutorialSetActiveChapter(st, &ch), 0);
    CHECK(st.chapter == &ch);
}

// --- ClassifyAdvance: null chapter / zero steps ------------------------------
TEST(WorldTutorialCore, ClassifyNullChapterAndNoSteps) {
    TutorialState st = MakeState(nullptr, 0, 0);
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kNoChapter);
    CHECK_EQ(TutorialAdvanceReturn(TutorialClassifyAdvance(st)), -4);

    TutorialChapter empty{}; empty.stepCount = 0; empty.steps = nullptr;
    st.chapter = &empty;
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kStopNoSteps);
    CHECK_EQ(TutorialAdvanceReturn(TutorialClassifyAdvance(st)), 0);
}

// --- ClassifyAdvance: step index AT and PAST the step count -> kEndChapter ----
// Hardening: a stepIndex >= total must take the end path WITHOUT indexing the
// step array (the original's `v1 <= idx` branch destroys the form first).
TEST(WorldTutorialCore, ClassifyStepIndexOutOfRangeEndsChapter) {
    TutorialStep steps[2] = { MakeStep(0, 0), MakeStep(1, 1) };
    TutorialChapter ch{}; ch.stepCount = 2; ch.steps = steps;

    // index == count
    TutorialState atEnd = MakeState(&ch, 2, 0);
    CHECK(TutorialClassifyAdvance(atEnd) == TutorialAdvance::kEndChapter);
    // index far past count (a corrupt save) -> still kEndChapter, no array read
    TutorialState past = MakeState(&ch, 1000, 0);
    CHECK(TutorialClassifyAdvance(past) == TutorialAdvance::kEndChapter);
}

// --- ClassifyAdvance: null step array with a non-zero count -> end ------------
TEST(WorldTutorialCore, ClassifyNullStepsArrayEnds) {
    TutorialChapter ch{}; ch.stepCount = 3; ch.steps = nullptr;  // count says 3, no array
    TutorialState st = MakeState(&ch, 0, 0);
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kEndChapter);
}

// --- ClassifyAdvance: reuse vs rebuild vs end-of-steps -----------------------
TEST(WorldTutorialCore, ClassifyReuseRebuildEndOfSteps) {
    TutorialStep steps[3] = {
        MakeStep(/*phase=*/5, /*formPos=*/0),
        MakeStep(7, 1),
        MakeStep(9, kTutorialPosEnd),   // form-pos >= 4 -> end of steps
    };
    TutorialChapter ch{}; ch.stepCount = 3; ch.steps = steps;

    // step 0, lastPhase == its phase -> reuse
    CHECK(TutorialClassifyAdvance(MakeState(&ch, 0, 5)) == TutorialAdvance::kReuseForm);
    // step 1, lastPhase differs, formPos 1 (<4) -> rebuild
    CHECK(TutorialClassifyAdvance(MakeState(&ch, 1, 0)) == TutorialAdvance::kRebuildForm);
    // step 2, lastPhase differs, formPos == 4 -> end of steps
    CHECK(TutorialClassifyAdvance(MakeState(&ch, 2, 0)) == TutorialAdvance::kEndOfSteps);
    CHECK_EQ(TutorialAdvanceReturn(TutorialAdvance::kEndOfSteps), -4);
}

// --- CommitStep stores the phase -------------------------------------------
TEST(WorldTutorialCore, CommitStepStoresPhase) {
    TutorialState st = MakeState(nullptr, 0, 0);
    TutorialStep s = MakeStep(42, 1);
    TutorialCommitStep(st, s);
    CHECK_EQ((int)st.lastPhase, 42);
    CHECK(st.formOpen);
}

// --- ChainLength: 0 for null, counts the +112 chain -------------------------
TEST(WorldTutorialCore, ChainLengthBoundary) {
    CHECK_EQ(TutorialChainLength(nullptr), 0);          // empty chain
    TutorialChapter c2{}; c2.next = nullptr;
    TutorialChapter c1{}; c1.next = &c2;
    TutorialChapter c0{}; c0.next = &c1;
    CHECK_EQ(TutorialChainLength(&c0), 3);
    CHECK_EQ(TutorialChainLength(&c2), 1);              // single node
}
