// Unit tests for cutscene_misc4 — the deterministic kernels of
// RunParticipants / SalonFadeTransition / LeaseWindow, plus the full-flow
// drivers over the inert hook surface. Golden vectors computed with python3.
#include "test.h"

#include "sim/cutscene_misc4.h"
#include "sim/cutscene.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
CutsceneSlot MakeSlot(const i32* ids, int n, i32 master = -1, i32 id = 7) {
    CutsceneSlot s{};
    std::memset(&s, 0, sizeof(s));
    s.id = id;
    s.master = master;
    s.partCount = static_cast<u8>(n);
    for (int i = 0; i < kMaxParticipants; ++i) s.partIds[i] = -1;
    for (int i = 0; i < n && i < kMaxParticipants; ++i) s.partIds[i] = ids[i];
    return s;
}
}  // namespace

// --- forward participant scan ------------------------------------------------
TEST(CutsceneMisc4, FindParticipantById) {
    i32 ids[] = {10, 20, -1, 50};
    CutsceneSlot s = MakeSlot(ids, 4);
    CHECK_EQ(CutsceneFindParticipantById(&s, 10), 0);
    CHECK_EQ(CutsceneFindParticipantById(&s, 20), 1);
    CHECK_EQ(CutsceneFindParticipantById(&s, 50), 3);
    CHECK_EQ(CutsceneFindParticipantById(&s, 999), -1);
    CHECK_EQ(CutsceneFindParticipantById(&s, -1), 2);  // empty cell matches -1
    CHECK_EQ(CutsceneFindParticipantById(nullptr, 10), -1);
}

// --- backward "find prior valid" scan ---------------------------------------
TEST(CutsceneMisc4, FindPriorValidParticipant) {
    i32 ids[] = {10, 20, -1, -1, 50, -1, -1, -1};
    CutsceneSlot s = MakeSlot(ids, 8);
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 5), 4);
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 4), 1);
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 8), 4);
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 1), 0);  // idx<=0 -> 0
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 2), 1);
    CHECK_EQ(CutsceneFindPriorValidParticipant(&s, 0), 0);
    CHECK_EQ(CutsceneFindPriorValidParticipant(nullptr, 5), 0);
}

// --- run-mode classifier ----------------------------------------------------
TEST(CutsceneMisc4, ClassifyRunMode) {
    CHECK_EQ(CutsceneClassifyRunMode(1), 1);    // master
    CHECK_EQ(CutsceneClassifyRunMode(0), 0);    // non-master
    CHECK_EQ(CutsceneClassifyRunMode(2), -1);   // skip
    CHECK_EQ(CutsceneClassifyRunMode(255), -1); // skip
}

// --- per-participant callback table -----------------------------------------
TEST(CutsceneMisc4, ParticipantCallbackTable) {
    CHECK_EQ(CutsceneParticipantCallbackTable(6), 4);  // weddingA -> 5C4
    CHECK_EQ(CutsceneParticipantCallbackTable(7), 0);  // weddingB -> skip
    CHECK_EQ(CutsceneParticipantCallbackTable(0), 8);  // other -> 5C8
    CHECK_EQ(CutsceneParticipantCallbackTable(3), 8);
}

// --- salon transition classifier --------------------------------------------
TEST(CutsceneMisc4, SalonClassifyTransition) {
    // replay-gated -> 0 regardless.
    CHECK_EQ(CutsceneSalonClassifyTransition(1, 100, 2, 100, 2, 200, 3), 0);
    // matches first cached scene -> 1.
    CHECK_EQ(CutsceneSalonClassifyTransition(0, 100, 2, 100, 2, 200, 3), 1);
    // matches second cached scene -> 1.
    CHECK_EQ(CutsceneSalonClassifyTransition(0, 200, 3, 100, 2, 200, 3), 1);
    // entity matches but season differs -> 2 (load).
    CHECK_EQ(CutsceneSalonClassifyTransition(0, 100, 9, 100, 2, 200, 3), 2);
    // no match -> 2 (load).
    CHECK_EQ(CutsceneSalonClassifyTransition(0, 500, 1, 100, 2, 200, 3), 2);
}

// --- salon scene kind dispatch ----------------------------------------------
TEST(CutsceneMisc4, SalonSceneKind) {
    CHECK_EQ(CutsceneSalonSceneKind(true, 99), 0);    // production -> Gebaeude
    CHECK_EQ(CutsceneSalonSceneKind(false, 5), 1);    // objekt
    CHECK_EQ(CutsceneSalonSceneKind(false, -2), -1);  // skip (0xFE)
}

// --- lease offer scan -------------------------------------------------------
TEST(CutsceneMisc4, LeaseFindOffer) {
    i32 lessees[] = {11, 22, 33, 44};
    i32 valid[]   = { 1,  1, -1,  1};
    CHECK_EQ(CutsceneLeaseFindOffer(lessees, valid, 4, 11), 0);
    CHECK_EQ(CutsceneLeaseFindOffer(lessees, valid, 4, 22), 1);
    CHECK_EQ(CutsceneLeaseFindOffer(lessees, valid, 4, 33), -1); // valid==-1
    CHECK_EQ(CutsceneLeaseFindOffer(lessees, valid, 4, 44), 3);
    CHECK_EQ(CutsceneLeaseFindOffer(lessees, valid, 4, 99), -1); // not found
    CHECK_EQ(CutsceneLeaseFindOffer(nullptr, valid, 4, 11), -1);
}

// --- lease affordability ----------------------------------------------------
TEST(CutsceneMisc4, LeaseCanAfford) {
    CHECK(CutsceneLeaseCanAfford(1000, 800));
    CHECK(!CutsceneLeaseCanAfford(700, 800));
    CHECK(CutsceneLeaseCanAfford(800, 800));   // funds == base
    CHECK(!CutsceneLeaseCanAfford(100, 0));     // base<1 -> ClampValueRange fails
    CHECK(!CutsceneLeaseCanAfford(100, -5));
}

// --- RunParticipants driver: inert path is deterministic --------------------
TEST(CutsceneMisc4, RunParticipantsInertRestoresSeedAndBumpsMode) {
    Cutscene3() = Cutscene3State{};
    CutsceneMisc4Hooks h{};
    SetCutsceneMisc4Hooks(&h);
    CutsceneRng rng;
    rng.SetSeed(0x1234);
    i32 ids[] = {10, 20};
    CutsceneSlot s = MakeSlot(ids, 2, /*master=*/10);

    int before = Cutscene3().duelMode;
    i32 ret = CutsceneRunParticipants(rng, &s, /*flags=*/0, /*mode=*/2, /*cut=*/1);
    CHECK_EQ(ret, 0x1234);              // seed restored
    CHECK_EQ(rng.GetSeed(), 0x1234);
    CHECK_EQ(Cutscene3().duelMode, before + 1);  // ++dword_6315A4 on exit
    SetCutsceneMisc4Hooks(nullptr);
}

TEST(CutsceneMisc4, RunParticipantsNullSlotSafe) {
    Cutscene3() = Cutscene3State{};
    SetCutsceneMisc4Hooks(nullptr);
    CutsceneRng rng;
    rng.SetSeed(77);
    i32 ret = CutsceneRunParticipants(rng, nullptr, 0xC, 1, 0);
    CHECK_EQ(ret, 77);
    CHECK_EQ(rng.GetSeed(), 77);
}

// --- SalonFadeTransition driver ---------------------------------------------
TEST(CutsceneMisc4, SalonFadeTransitionGatedAndFade) {
    SetCutsceneMisc4Hooks(nullptr);
    Cutscene3() = Cutscene3State{};
    Cutscene3().replayGate = 1;
    // gated -> 0
    CHECK_EQ(CutsceneSalonFadeTransition(100, 2, 100, 2, 0, 0, true, 0), 0);
    Cutscene3().replayGate = 0;
    // cache match -> 1
    CHECK_EQ(CutsceneSalonFadeTransition(100, 2, 100, 2, 0, 0, true, 0), 1);
    // load -> 2
    CHECK_EQ(CutsceneSalonFadeTransition(500, 9, 100, 2, 0, 0, false, 7), 2);
}

// --- LeaseWindow driver -----------------------------------------------------
TEST(CutsceneMisc4, LeaseWindowInertNotAffordable) {
    SetCutsceneMisc4Hooks(nullptr);
    i32 lessees[] = {11};
    i32 valid[]   = {1};
    i32 rent = -1;
    // No personFind -> rec null -> returns 0, rent stays at base.
    int r = CutsceneLeaseWindow(lessees, valid, 1, 11, /*funds=*/9999,
                                /*baseRent=*/500, &rent);
    CHECK_EQ(r, 0);
    CHECK_EQ(rent, 500);
}

TEST(CutsceneMisc4, LeaseWindowOfferNotFound) {
    SetCutsceneMisc4Hooks(nullptr);
    i32 lessees[] = {11};
    i32 valid[]   = {1};
    i32 rent = -1;
    int r = CutsceneLeaseWindow(lessees, valid, 1, 99, 9999, 500, &rent);
    CHECK_EQ(r, 0);
    CHECK_EQ(rent, 500);  // a1[37] default == base rent
}
