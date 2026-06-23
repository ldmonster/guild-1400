// Golden-vector tests for the VIBE_Character morph/fade cluster (recon5).
#include "tests/framework/test.h"

#include "src/sim/character_recon5_morph.h"

using namespace guild::sim;

// ---- FadeComputeValue: the ramp math --------------------------------------

TEST(CharacterRecon5Morph, FadeInRamp) {
    FadeSlot s; s.id=1; s.dir=1; s.start=100;   // fade IN
    // now-start = 25 -> 25/50 = 0.5 -> *255 = 127.5
    FadeStep r = FadeComputeValue(s, 125);
    CHECK_EQ(r.ramp01, 0.5f);
    CHECK_EQ(r.value255, 127.5f);
    CHECK(!r.finished);
}

TEST(CharacterRecon5Morph, FadeOutRamp) {
    FadeSlot s; s.id=1; s.dir=0; s.start=100;   // fade OUT: 1 - (now-start)/50
    // now-start=25 -> 1-0.5 = 0.5 -> 127.5
    FadeStep r = FadeComputeValue(s, 125);
    CHECK_EQ(r.ramp01, 0.5f);
    CHECK_EQ(r.value255, 127.5f);
}

TEST(CharacterRecon5Morph, FadeInClampLowAtStart) {
    FadeSlot s; s.id=1; s.dir=1; s.start=100;
    FadeStep r = FadeComputeValue(s, 100);   // t=0 -> ramp 0 -> finished(invisible)
    CHECK_EQ(r.ramp01, 0.0f);
    CHECK_EQ(r.value255, 0.0f);
    CHECK(r.finished);
    CHECK(r.madeInvisible);
    CHECK(!r.madeVisible);
}

TEST(CharacterRecon5Morph, FadeInReachesFull) {
    FadeSlot s; s.id=1; s.dir=1; s.start=100;
    FadeStep r = FadeComputeValue(s, 100+50);  // t=1 -> 255 -> finished(visible)
    CHECK_EQ(r.value255, 255.0f);
    CHECK(r.finished);
    CHECK(r.madeVisible);
    CHECK(!r.madeInvisible);
}

TEST(CharacterRecon5Morph, FadeOutReachesZero) {
    FadeSlot s; s.id=1; s.dir=0; s.start=100;
    FadeStep r = FadeComputeValue(s, 100+50);  // 1-1=0 -> 0 -> invisible
    CHECK_EQ(r.value255, 0.0f);
    CHECK(r.finished);
    CHECK(r.madeInvisible);
}

TEST(CharacterRecon5Morph, FadeNegativeClampsToZero) {
    FadeSlot s; s.id=1; s.dir=1; s.start=200;
    FadeStep r = FadeComputeValue(s, 100);   // negative -> clamp 0
    CHECK_EQ(r.value255, 0.0f);
}

// ---- FadeOutSlots: the table walk -----------------------------------------

TEST(CharacterRecon5Morph, FadeSlotsSkipsEmptyAndStepsLive) {
    TObject obj; obj.sceneCtx = 5;
    TChar ch; ch.object=&obj; ch.f140 = 0x40;
    FadeSlot slots[2];
    slots[0].id = 0;                  // empty -> skipped
    slots[1].id = 1; slots[1].dir = 1; slots[1].start = 100; slots[1].ch = &ch;

    static int transparencyCalls; transparencyCalls = 0;
    static int lastPacked; lastPacked = 999;
    FadeHooks H;
    H.changeTransparency = [](TObject*, int, int packed, FadeSlot*){ transparencyCalls++; lastPacked = packed; };
    H.setVisible = [](TChar*, int){};

    // mid ramp (t=0.5 -> 127.5): one transparency call, slot stays live
    FadeOutSlots(slots, 2, 125, H);
    CHECK_EQ(transparencyCalls, 1);
    CHECK_EQ(lastPacked, 127);        // (int)127.5
    CHECK_EQ(slots[1].id, 1);         // not finished
}

TEST(CharacterRecon5Morph, FadeSlotsEndpointClearsSlotAndVisibility) {
    TObject obj; obj.sceneCtx = 5;
    TChar ch; ch.object=&obj; ch.f140 = 0x40;
    FadeSlot slots[1];
    slots[0].id=1; slots[0].dir=1; slots[0].start=100; slots[0].ch=&ch;  // fade in

    static int visibleArg; visibleArg = -99;
    FadeHooks H;
    H.changeTransparency = [](TObject*, int, int, FadeSlot*){};
    H.setVisible = [](TChar*, int v){ visibleArg = v; };

    FadeOutSlots(slots, 1, 150, H);   // t=1 -> full -> visible 1, slot freed
    CHECK_EQ(slots[0].id, 0);         // slot cleared
    CHECK_EQ(visibleArg, 1);          // fade-in completed -> visible
    CHECK_EQ((int)(ch.f140 & 0x40), 0);  // fade flag cleared
}

TEST(CharacterRecon5Morph, FadeSlotsFadeOutEndpointHides) {
    TObject obj; TChar ch; ch.object=&obj; ch.f140=0x40;
    FadeSlot slots[1];
    slots[0].id=1; slots[0].dir=0; slots[0].start=100; slots[0].ch=&ch;  // fade out
    static int visibleArg; visibleArg=-99;
    FadeHooks H;
    H.changeTransparency=[](TObject*,int,int,FadeSlot*){};
    H.setVisible=[](TChar*,int v){ visibleArg=v; };
    FadeOutSlots(slots, 1, 150, H);   // 1-1=0 -> invisible
    CHECK_EQ(slots[0].id, 0);
    CHECK_EQ(visibleArg, 0);
}

// --- HARDENING: FadeOutSlots degenerate inputs ------------------------------
// count==0 walks no slots (no read of slots[]); a live slot whose character is
// null (a "bad" fade record) must be stepped without dereferencing the missing
// char/object — the obj-null guards skip the transparency/visibility leaves and
// the endpoint still clears the slot.
TEST(CharacterRecon5Morph, FadeSlotsCountZeroIsNoOp) {
    FadeSlot slots[1];
    slots[0].id = 1; slots[0].dir = 1; slots[0].start = 100; slots[0].ch = nullptr;
    static int calls; calls = 0;
    FadeHooks H;
    H.changeTransparency = [](TObject*, int, int, FadeSlot*){ calls++; };
    H.setVisible = [](TChar*, int){ calls++; };
    FadeOutSlots(slots, 0, 125, H);   // count 0 -> nothing touched
    CHECK_EQ(calls, 0);
    CHECK_EQ(slots[0].id, 1);          // untouched
}

TEST(CharacterRecon5Morph, FadeSlotsNullCharRecordSafe) {
    FadeSlot slots[1];
    // Live slot but ch (and therefore object) is null -> a degenerate fade record.
    slots[0].id = 1; slots[0].dir = 0; slots[0].start = 100; slots[0].ch = nullptr;
    static int transp; transp = 0; static int vis; vis = 0;
    FadeHooks H;
    H.changeTransparency = [](TObject*, int, int, FadeSlot*){ transp++; };
    H.setVisible = [](TChar*, int){ vis++; };
    // t=1 -> fade-out endpoint (zero). With no object/char, the transparency and
    // visibility leaves are guarded out, but the slot is still cleared. No crash.
    FadeOutSlots(slots, 1, 150, H);
    CHECK_EQ(transp, 0);
    CHECK_EQ(vis, 0);
    CHECK_EQ(slots[0].id, 0);          // endpoint reached -> slot freed
}

// ---- MorphComputeBlend ----------------------------------------------------

TEST(CharacterRecon5Morph, MorphBlendHalfAndFrames) {
    // first=10 last=40 next=20 -> v35 = 40-10 = 30 ; sum = 30+20 = 50 ; half=25
    // frames = (int)(25 + 0.5) = 25
    MorphKeyBlend b = MorphComputeBlend(10, 40, 20);
    CHECK_EQ(b.sumDelta, 50);
    CHECK_EQ(b.half, 25);
    CHECK_EQ(b.frames, 25);
}

TEST(CharacterRecon5Morph, MorphBlendOddHalfTruncates) {
    // first=0 last=5 next=2 -> v35=5 ; sum=7 ; half=7/2=3 ; frames=(int)(3.5)=3
    MorphKeyBlend b = MorphComputeBlend(0, 5, 2);
    CHECK_EQ(b.sumDelta, 7);
    CHECK_EQ(b.half, 3);
    CHECK_EQ(b.frames, 3);
}

// ---- ReleaseMorphAni ------------------------------------------------------

TEST(CharacterRecon5Morph, ReleaseMorphNoHandleNoOp) {
    TObject obj; TChar ch; ch.object=&obj; ch.morphAnim=0;
    MorphHooks H;
    static int releaseCalls; releaseCalls=0;
    H.releaseMeshData=[](int,int,int,int){ releaseCalls++; return 0; };
    ReleaseMorphAni(&ch, H);
    CHECK_EQ(releaseCalls, 0);
    CHECK_EQ(ch.morphAnim, 0);
}

TEST(CharacterRecon5Morph, ReleaseMorphClearsHandle) {
    TObject obj; TChar ch; ch.object=&obj; ch.morphAnim=123;
    MorphHooks H;
    static int releaseCalls; releaseCalls=0;
    H.pruneExpired=[](int){};
    H.findFreeMeshSlot=[](){ return 4; };
    H.releaseMeshData=[](int,int,int,int){ releaseCalls++; return 0; };
    ReleaseMorphAni(&ch, H);
    CHECK_EQ(releaseCalls, 1);
    CHECK_EQ(ch.morphAnim, 0);
}

// ---- CheckAniMorph (state machine) ----------------------------------------

TEST(CharacterRecon5Morph, CheckAniMorphNoPendingNoOp) {
    TObject obj; TChar ch; ch.object=&obj; ch.pendingStream=0;
    MorphHooks H;
    CheckAniMorph(&ch, H);   // returns early; no crash, no state change
    CHECK_EQ(ch.primaryAnim, 0);
}

TEST(CharacterRecon5Morph, CheckAniMorphAttachesPendingAsPrimary) {
    TObject obj; TChar ch; ch.object=&obj;
    ch.primaryAnim=0; ch.morphAnim=0; ch.pendingStream=99; ch.morphMode=1;
    MorphHooks H;
    H.findFreeMeshSlot=[](){ return 1; };   // nonzero -> skip LoadStreamToStock
    H.attachToBone=[](int,int){ return 7777; };
    H.indexFromPointer=[](int){ return 0; };
    CheckAniMorph(&ch, H);
    CHECK_EQ(ch.primaryAnim, 7777);   // pending attached as primary
    CHECK_EQ(ch.pendingStream, 0);    // pending consumed
    CHECK_EQ((int)ch.morphMark, -1);  // mark set
}
