// Integration tests: the chapter 3/4/5 step tables wired against the REAL tutorial
// runtime siblings — the advance state machine (world/tutorial.cpp) and the chapter
// 1/2 chain builders (world/tutorial_steps.cpp). No mocks: these exercise the actual
// cross-module contracts (shared TutorialChapterNode, chain walk, advance machine).
#include "test.h"

#include "world/tutorial.h"
#include "world/tutorial_steps.h"
#include "world/tutorial_chapters345.h"

using namespace guild::world;

// Test-only definition of the cross-module screen-resolution global that the chapter
// builders branch on (owned by gui/widget_create.cpp in the real build). Provided
// here so the isolated test binary links without pulling the whole gui graph.
namespace guild::gui { guild::i32 g_screenClipExt = 0; }

// All five chapters materialise into one contiguous chain and the REAL chain walker
// (TutorialNodeChainLength) reports the right length for each.
TEST(TutCh345Itest, AllChaptersChainWithRealWalker) {
    int n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0;
    const TutorialNodeSpec*   s1 = TutorialChapter1Steps(&n1);   // sibling (ch1/2)
    const TutorialNodeSpec*   s2 = TutorialChapter2Steps(&n2);
    const TutorialNodeSpecEx* s3 = TutorialChapter3Steps(&n3);   // this module
    const TutorialNodeSpecEx* s4 = TutorialChapter4Steps(&n4);
    const TutorialNodeSpecEx* s5 = TutorialChapter5Steps(&n5);

    TutorialChapterNode b1[16], b2[16], b3[16], b4[16], b5[16];
    // ch1/2 via the sibling builder, ch3/4/5 via this module's builder.
    CHECK_EQ(TutorialNodeChainLength(TutorialBuildChain(s1, n1, b1)),   n1);
    CHECK_EQ(TutorialNodeChainLength(TutorialBuildChain(s2, n2, b2)),   n2);
    CHECK_EQ(TutorialNodeChainLength(TutorialBuildChainEx(s3, n3, b3)), n3);
    CHECK_EQ(TutorialNodeChainLength(TutorialBuildChainEx(s4, n4, b4)), n4);
    CHECK_EQ(TutorialNodeChainLength(TutorialBuildChainEx(s5, n5, b5)), n5);
    // ch1/2 counts come from the sibling builders (intro + steps + outro); ch3/4/5
    // are this module's (10/5/8). Assert each against the recovered node count.
    CHECK_EQ(n1, 11);
    CHECK_EQ(n2, 9);
    CHECK_EQ(n3, 10);
    CHECK_EQ(n4, 5);
    CHECK_EQ(n5, 8);
}

// The chapter-3 step nodes drive the runtime advance machine: build a runtime chapter
// whose inner steps mirror the chapter-3 node's formType/arrow and verify the REAL
// classifier (world/tutorial.cpp) reuses vs rebuilds vs ends correctly.
TEST(TutCh345Itest, RunnerAdvanceOverChapter3Steps) {
    int n3 = 0;
    const TutorialNodeSpecEx* c3 = TutorialChapter3Steps(&n3);

    // Two steps with distinct phase bytes => the classifier must rebuild the form on
    // the transition; matching phase => reuse. Use the chapter-3 nodes' formType as
    // the form-position seed (clamped to the 0..3 highlight-form range).
    TutorialStep steps[2]{};
    steps[0].phase   = 1;
    steps[0].formPos = static_cast<guild::u8>(c3[1].formType % 4);  // node A
    steps[1].phase   = 2;
    steps[1].formPos = static_cast<guild::u8>(c3[2].formType % 4);  // node B
    TutorialChapter ch{};
    ch.stepCount = 2;
    ch.steps     = steps;

    TutorialState st{};
    st.active    = true;
    st.chapter   = &ch;
    st.stepIndex = 0;
    st.lastPhase = 1;                       // equals steps[0].phase

    // step 0: same phase -> reuse
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kReuseForm);
    TutorialCommitStep(st, steps[0]);
    st.stepIndex = 1;
    // step 1: phase changed (1 -> 2), formPos < 4 -> rebuild
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kRebuildForm);
    CHECK_EQ(TutorialAdvanceReturn(TutorialClassifyAdvance(st)), 0);
    // past the end -> chapter done
    st.stepIndex = 2;
    CHECK(TutorialClassifyAdvance(st) == TutorialAdvance::kEndChapter);
}

// AdvanceChapterOrFree against the REAL SetActiveChapter (no stubbing): arming a
// chapter clears the chain link; an inactive runtime takes the free path.
TEST(TutCh345Itest, AdvanceChapterOrFreeAgainstRealSetActive) {
    int n4 = 0;
    const TutorialNodeSpecEx* c4 = TutorialChapter4Steps(&n4);
    TutorialChapterNode nodes[8];
    TutorialBuildChainEx(c4, n4, nodes);

    TutorialChapter ch{};                   // a runtime chapter to arm
    TutorialChapter next{};
    ch.next = &next;

    TutorialState st{};
    st.active = true;
    bool freed = true;
    CHECK(TutorialAdvanceChapterOrFree(st, &ch, &freed));
    CHECK(!freed);
    CHECK_EQ(st.chapter, &ch);
    CHECK_EQ(ch.next, (TutorialChapter*)nullptr);   // +112 cleared on arm

    st.active = false;                       // inactive -> SetActiveChapter == -4
    CHECK(!TutorialAdvanceChapterOrFree(st, &ch, &freed));
    CHECK(freed);
    (void)nodes;
}
