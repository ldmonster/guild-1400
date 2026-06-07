// Unit tests for the chapter 3/4/5 tutorial step tables and the AdvanceChapterOrFree
// runner. Golden vectors taken byte-for-byte from the decompiled builders
// (gilde.exe InitChapter3/4/5Steps + AdvanceChapterOrFree).
#include "test.h"

#include <cstring>

#include "world/tutorial_chapters345.h"

using namespace guild::world;

// ---------------------------------------------------------------------------
// Node counts (one record per VIBE_Memory_AllocDebug 0x74 in each builder).
// ---------------------------------------------------------------------------
TEST(TutCh345, NodeCounts) {
    int c3 = 0, c4 = 0, c5 = 0;
    TutorialChapter3Steps(&c3);
    TutorialChapter4Steps(&c4);
    TutorialChapter5Steps(&c5);
    CHECK_EQ(c3, 10);   // intro + A,B,D,E,F,G,H,I + outro
    CHECK_EQ(c4, 5);    // intro + D,B,E + outro
    CHECK_EQ(c5, 8);    // intro + B,D,C,C1,E,F,G
}

// ---------------------------------------------------------------------------
// Chapter 3 intro/outro golden fields.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter3IntroOutro) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter3Steps(&n);
    const TutorialNodeSpecEx& intro = c[0];
    CHECK_EQ(intro.kind, 3);
    CHECK(std::strcmp(intro.name, "3rtn") == 0);
    CHECK_EQ(intro.mainTextId, 7434);
    CHECK(std::strcmp(intro.mainRes, "CHAPTER_3_INTRO") == 0);
    CHECK_EQ(intro.voiceA, -1);
    CHECK_EQ(intro.coordB, 142);
    CHECK(intro.cb0 == TutorialCallback345::kOpenTownHallDialog);
    CHECK_EQ(intro.formType, (guild::u8)11);
    CHECK_EQ(intro.mask, 22583);
    CHECK_EQ(intro.word54, (guild::i16)7680);

    const TutorialNodeSpecEx& outro = c[9];
    CHECK_EQ(outro.kind, 7);                       // outro marker
    CHECK(std::strcmp(outro.name, "3rtu") == 0);
    CHECK_EQ(outro.nameByte8, (guild::u8)3);       // builder: v87[8] = 3
    CHECK_EQ(outro.mainTextId, 7480);
    CHECK_EQ(outro.coordB, 355);
    CHECK_EQ(outro.word54, (guild::i16)8048);
}

// ---------------------------------------------------------------------------
// Chapter 3 node B: carries script + anim + the carry/drop callback trio.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter3NodeB) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter3Steps(&n);
    const TutorialNodeSpecEx& b = c[2];            // intro,A,B
    CHECK_EQ(b.mainTextId, 7443);
    CHECK_EQ(b.remindTextId, 7448);
    CHECK_EQ(b.auxTextId, 7447);
    CHECK(std::strcmp(b.auxRes, "Chapter_3_B_Step_Aux") == 0);
    CHECK_EQ(b.doneTextId, 7449);
    CHECK_EQ(b.nameByte8, (guild::u8)2);
    CHECK(b.cb0 == TutorialCallback345::kDispatchCarryToEntrance);
    CHECK(b.cb2 == TutorialCallback345::kHandleEvent32DropOnShop);
    CHECK_EQ(b.arrowTarget, (guild::u8)31);
    CHECK_EQ(b.formType, (guild::u8)9);
    CHECK_EQ(b.extra19, 32);                        // idx19 = 32
    CHECK_EQ(b.field80, 2);                         // idx20 = 2 script entries
    CHECK_EQ(b.word54, (guild::i16)7728);
    // script sub-table (2 entries)
    CHECK(b.script != nullptr);
    CHECK_EQ(b.script[0].opcode, 710);
    CHECK_EQ(b.script[0].pos, (guild::u8)1);
    CHECK_EQ(b.script[0].textId, 7445);
    CHECK(std::strcmp(b.script[0].res, "CHAPTER_3_B_STEP_RIGHT") == 0);
    CHECK_EQ(b.script[1].pos, (guild::u8)0);
    CHECK_EQ(b.script[1].textId, 7446);
    // anim sub-table (1 frame)
    CHECK(b.anim != nullptr);
    CHECK_EQ(b.anim->frameCount, 1);
}

// ---------------------------------------------------------------------------
// Anim coordinate golden vectors (resolution-keyed). Chapter 3 node B, 1 frame.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter3NodeBAnimCoords) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter3Steps(&n);
    const TutorialAnimTable& a = *c[2].anim;
    // 800: 695,300,626,400,366,380,376,289,35,142,71
    CHECK_EQ(a.frames800[0], 695.0f);
    CHECK_EQ(a.frames800[1], 300.0f);
    CHECK_EQ(a.frames800[7], 289.0f);
    CHECK_EQ(a.frames800[10], 71.0f);
    // 1024
    CHECK_EQ(a.frames1024[0], 920.0f);
    CHECK_EQ(a.frames1024[6], 480.0f);
    // else (1280)
    CHECK_EQ(a.framesElse[0], 1026.0f);
    CHECK_EQ(a.framesElse[5], 600.0f);
    // resolution selector
    CHECK_EQ(TutorialAnimFramesForResolution(a, 800),  a.frames800);
    CHECK_EQ(TutorialAnimFramesForResolution(a, 1024), a.frames1024);
    CHECK_EQ(TutorialAnimFramesForResolution(a, 1280), a.framesElse);
    CHECK_EQ(TutorialAnimFramesForResolution(a, 640),  a.framesElse);
}

// ---------------------------------------------------------------------------
// Chapter 3 node G: 5-frame anim, multi-stage drop, query callback at idx24.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter3NodeGAnim) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter3Steps(&n);
    const TutorialNodeSpecEx& g = c[6];            // intro,A,B,D,E,F,G -> index 6
    CHECK_EQ(g.mainTextId, 7468);
    CHECK(g.cb2 == TutorialCallback345::kHandleMultiStageDrop);
    CHECK(g.animCb == TutorialCallback345::kQueryEventStateRange);
    CHECK(g.anim != nullptr);
    CHECK_EQ(g.anim->frameCount, 5);
    // frame 0 (800): 465,195,536,440,...
    CHECK_EQ(g.anim->frames800[0], 465.0f);
    CHECK_EQ(g.anim->frames800[2], 536.0f);
    // frame 3 (else): 560,420,710,370,860,320,1020,310
    CHECK_EQ(g.anim->framesElse[3 * 11 + 0], 560.0f);
    CHECK_EQ(g.anim->framesElse[3 * 11 + 6], 1020.0f);
}

// ---------------------------------------------------------------------------
// Chapter 4: simple 5-node chain, the workshop/storage/house drop callbacks.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter4Callbacks) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter4Steps(&n);
    CHECK_EQ(c[0].kind, 4);
    CHECK_EQ(c[4].kind, 4);
    CHECK_EQ(c[4].mask, 6199);                      // outro mask
    // node D (idx1)
    CHECK_EQ(c[1].kind, 1);                         // builder writes 1 for the inner step
    CHECK_EQ(c[1].mainTextId, 7484);
    CHECK(c[1].cb0 == TutorialCallback345::kQueueRequestIfNotHandled);
    CHECK(c[1].cb1 == TutorialCallback345::kHandleWorkshopDropStep);
    CHECK(c[1].cb2 == TutorialCallback345::kAllowWorkshopDropTarget);
    CHECK_EQ(c[1].mask, 23095);
    // node B
    CHECK(c[2].cb1 == TutorialCallback345::kHandleStorageDropStep);
    CHECK_EQ(c[2].mask, 23415);
    // node E
    CHECK(c[3].cb1 == TutorialCallback345::kHandleHouseDropStep);
    CHECK_EQ(c[3].mask, 24439);
    CHECK(std::strcmp(TutorialChapter4VoiceBank(), "TUTORIAL_CHAPTER_4.sbf") == 0);
}

// ---------------------------------------------------------------------------
// Chapter 5: final node kind 6 (no outro), the signed word54, node C anim.
// ---------------------------------------------------------------------------
TEST(TutCh345, Chapter5Tail) {
    int n = 0;
    const TutorialNodeSpecEx* c = TutorialChapter5Steps(&n);
    CHECK_EQ(c[0].kind, 5);
    CHECK(c[0].cb0 == TutorialCallback345::kOpenActiveCharBuilding);
    // node C (idx3): anim + work-at-shop
    const TutorialNodeSpecEx& nodeC = c[3];
    CHECK_EQ(nodeC.mainTextId, 7507);
    CHECK(nodeC.cb0 == TutorialCallback345::kStartWorkAtShopSlot);
    CHECK(nodeC.animCb == TutorialCallback345::kQueryEventCodeRange146);
    CHECK(nodeC.anim != nullptr);
    CHECK_EQ(nodeC.anim->frameCount, 2);
    CHECK_EQ(nodeC.anim->frames800[0], 140.0f);
    // node F has the signed word54 = -16520
    CHECK_EQ(c[6].word54, (guild::i16)-16520);
    CHECK(c[6].cb0 == TutorialCallback345::kHandleCandidacyStep);
    // final node G: kind 6, submit assignments
    const TutorialNodeSpecEx& g = c[7];
    CHECK_EQ(g.kind, 6);
    CHECK_EQ(g.mainTextId, 7529);
    CHECK(g.cb0 == TutorialCallback345::kSubmitGuildMemberAssignments);
    CHECK_EQ(g.formType, (guild::u8)11);
    CHECK(std::strcmp(TutorialChapter5VoiceBank(), "TUTORIAL_CHAPTER_5.sbf") == 0);
}

// ---------------------------------------------------------------------------
// BuildChainEx: materialise the 116-byte node records + chain link semantics.
// ---------------------------------------------------------------------------
TEST(TutCh345, BuildChain3) {
    int n = 0;
    const TutorialNodeSpecEx* specs = TutorialChapter3Steps(&n);
    TutorialChapterNode nodes[10];
    TutorialChapterNode* head = TutorialBuildChainEx(specs, n, nodes);
    CHECK_EQ(head, &nodes[0]);
    CHECK_EQ(TutorialNodeChainLength(head), 10);
    // field copies
    CHECK_EQ(nodes[0].kind, 3);
    CHECK_EQ(nodes[0].mainTextId, 7434);
    CHECK_EQ(nodes[2].arrowTarget, (guild::u8)31);   // node B
    CHECK_EQ(nodes[2].name[4], (char)2);             // byte[8] = nameByte8
    CHECK_EQ((int)nodes[9].nextRaw, 0);              // last node terminates the chain
    CHECK_EQ((int)nodes[8].nextRaw, 1);              // earlier nodes link onward
    // name dword copied
    CHECK(std::strncmp(nodes[0].name, "3rtn", 4) == 0);
}

// ---------------------------------------------------------------------------
// AdvanceChapterOrFree: armed vs freed transitions (gilde.exe 0x4db8ac).
// ---------------------------------------------------------------------------
TEST(TutCh345, AdvanceChapterArmed) {
    TutorialState st{};
    st.active = true;                  // SetActiveChapter returns 0 -> armed
    TutorialChapter sibling{};
    TutorialChapter node{};
    node.next = &sibling;              // a chain link that must be cleared on arm
    bool freed = true;
    bool armed = TutorialAdvanceChapterOrFree(st, &node, &freed);
    CHECK(armed);
    CHECK(!freed);
    CHECK_EQ(st.chapter, &node);       // chapter request armed
    CHECK_EQ(node.next, (TutorialChapter*)nullptr);  // +112 cleared
}

TEST(TutCh345, AdvanceChapterFreed) {
    TutorialState st{};
    st.active = false;                 // SetActiveChapter returns -4 -> free path
    TutorialChapter node{};
    bool freed = false;
    bool armed = TutorialAdvanceChapterOrFree(st, &node, &freed);
    CHECK(!armed);
    CHECK(freed);                      // VIBE_He_FreeHandlerEntry path taken
}
