// Golden-vector unit tests for the cutscene_recon2 cluster:
//   VIBE_Movie_PlayOutro       (0x534924)
//   VIBE_Tutorial_HideDonePanelAndStopVoice (0x5976a8)
//   VIBE_Theatre_RunEventMenu  (0x536a30) — deterministic core helpers
//
// Self-contained: drives the reconstructed logic through injected hooks and checks
// the exact constants/branches against the decompile.

#include "tests/framework/test.h"
#include "play/cutscene_recon2_movie.h"
#include "play/cutscene_recon2_tutorial.h"
#include "play/cutscene_recon2_theatre.h"

#include <string>
#include <vector>

using namespace guild::play;

// ===========================================================================
// Movie — VIBE_Movie_PlayOutro
// ===========================================================================

TEST(Cutscene2ReconMovie, DllNotAvailableReturnsZeroNoSideEffects) {
    MovieDllHooks dll;          // bound == false
    MovieControlHooks ctl;
    MovieGlobals g;
    g.moviesEnabled = 7;        // distinctive sentinel
    bool musicTouched = false;
    ctl.musicSetTrackFade = [&](float, int){ musicTouched = true; };
    // dllAvailable = false -> LoadLibrary fails -> return 0, no globals touched.
    u8 r = Movie_PlayOutro(dll, ctl, g, "\\project\\movie\\", 640, 480, /*dllAvailable=*/false);
    CHECK_EQ((int)r, 0);
    CHECK_EQ((int)g.moviesEnabled, 7);     // untouched
    CHECK(!musicTouched);                  // returned before the music fade
    CHECK(!dll.bound);
}

TEST(Cutscene2ReconMovie, FullPlaybackSavesAndRestoresGlobals) {
    MovieDllHooks dll;
    MovieControlHooks ctl;
    MovieGlobals g;
    g.moviesEnabled = 1;
    g.musicVolume = 127;
    g.someFlag67225C = 9;
    g.renderMode = 1;

    std::vector<float> musicVols;
    std::string builtPath;
    int prepareEventArg = -1;
    bool played = false, presented = false, focusSet = false;
    int fadeStatusCalls = 0;

    ctl.musicSetTrackFade = [&](float v, int ms){ musicVols.push_back(v); (void)ms; };
    ctl.fadeRegister   = [&]() -> const guild::u8* { return nullptr; };
    // Fade reports done immediately (bit 0x4 set, timer 0.0 -> not(0>0) -> exit).
    ctl.fadeStatusByte = [&]() -> guild::u8 { ++fadeStatusCalls; return 0x4; };
    ctl.fadeTimer      = [&]() -> float { return 0.0f; };
    ctl.runFrameLoop   = [&](){};
    ctl.presentFrame   = [&](){ presented = true; };
    ctl.pollKeyboard   = [&](bool){};
    ctl.acquireMouse   = [&](bool){};
    ctl.pumpMessages   = [&](){};
    ctl.setFocus       = [&](){ focusSet = true; };
    ctl.sleep          = [&](guild::u32){};
    dll.init    = [&](){};
    dll.prepare = [&](void*, int ev){ prepareEventArg = ev; return 555; };
    dll.play    = [&](int){ played = true; };

    u8 r = Movie_PlayOutro(dll, ctl, g, "\\project\\movie\\", 640<<16, 480<<16, /*dllAvailable=*/true);

    // Returns the SAVED moviesEnabled (1) and restores it.
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)g.moviesEnabled, 1);
    CHECK_EQ((int)g.someFlag67225C, 0);    // cleared at 0x534B95
    CHECK(dll.bound);                       // one-time init ran
    CHECK_EQ(dll.handle, 555);              // mov_Prepare_ handle stored
    CHECK_EQ(prepareEventArg, kMoviePrepareEventArg);  // 138
    CHECK(played);
    CHECK(presented);                       // renderMode==1
    CHECK(focusSet);

    // Two music fades: 0.0 (out) then volume*scale (in).
    CHECK_EQ((int)musicVols.size(), 2);
    CHECK_EQ(musicVols[0], 0.0f);
    // 127 * (1/127-ish 0x3C010204) == kMovieMusicVolumeScale*127.
    float expectIn = 127.0f * kMovieMusicVolumeScale;
    CHECK(musicVols[1] == expectIn);
}

TEST(Cutscene2ReconMovie, FadeLoopSpinsUntilDone) {
    MovieDllHooks dll;
    MovieControlHooks ctl;
    MovieGlobals g;
    int frames = 0;
    int statusReads = 0;
    ctl.musicSetTrackFade = [&](float, int){};
    ctl.fadeRegister = [&]() -> const guild::u8* { return nullptr; };
    // First 3 reads: not done (status 0). Then done.
    ctl.fadeStatusByte = [&]() -> guild::u8 { ++statusReads; return statusReads > 3 ? 0x4 : 0x0; };
    ctl.fadeTimer = [&]() -> float { return 0.0f; };
    ctl.runFrameLoop = [&](){ ++frames; };
    ctl.presentFrame = [&](){};
    ctl.pollKeyboard = [&](bool){};
    ctl.acquireMouse = [&](bool){};
    ctl.pumpMessages = [&](){};
    ctl.setFocus = [&](){};
    ctl.sleep = [&](guild::u32){};
    dll.init = [&](){};
    dll.prepare = [&](void*, int){ return 1; };
    dll.play = [&](int){};
    Movie_PlayOutro(dll, ctl, g, "\\m\\", 0, 0, true);
    CHECK_EQ(frames, 3);   // spun 3 frames before the fade reported done
}

TEST(Cutscene2ReconMovie, FadeTimerHoldsLoopWhenArmed) {
    MovieDllHooks dll; MovieControlHooks ctl; MovieGlobals g;
    int frames = 0; int reads = 0;
    ctl.musicSetTrackFade=[&](float,int){};
    ctl.fadeRegister=[&]()->const guild::u8*{return nullptr;};
    ctl.fadeStatusByte=[&]()->guild::u8{return 0x4;};   // always "done" bit
    // timer < 0 (armed, == sentinel -1.0) keeps spinning since 0.0 > -1.0;
    // becomes 0.0 after 2 frames.
    ctl.fadeTimer=[&]()->float{ ++reads; return reads > 2 ? 0.0f : kMovieFadeTimerSentinel; };
    ctl.runFrameLoop=[&](){++frames;};
    ctl.presentFrame=[&](){}; ctl.pollKeyboard=[&](bool){}; ctl.acquireMouse=[&](bool){};
    ctl.pumpMessages=[&](){}; ctl.setFocus=[&](){}; ctl.sleep=[&](guild::u32){};
    dll.init=[&](){}; dll.prepare=[&](void*,int){return 1;}; dll.play=[&](int){};
    Movie_PlayOutro(dll, ctl, g, "\\m\\", 0, 0, true);
    CHECK_EQ(frames, 2);
}

// ===========================================================================
// Tutorial — VIBE_Tutorial_HideDonePanelAndStopVoice
// ===========================================================================

TEST(Cutscene2ReconTutorial, HidesPanelStopsPlayingVoiceClearsHandle) {
    TutorialController c; c.panelForm = 0x1234; c.voiceHandle = 77;
    TutorialHooks h;
    int selForm = -1, hideForm = -1, stopped = -1; std::string token;
    h.selectWindow = [&](guild::i32 f){ selForm = f; };
    h.renderRichString = [&](const char* t){ token = t; };
    h.setObjectsVisible = [&](guild::i32 f){ hideForm = f; };
    h.voiceIsPlaying = [&](guild::i32){ return true; };
    h.stopVoice = [&](guild::i32 v){ stopped = v; };
    Tutorial_HideDonePanelAndStopVoice(c, h);
    CHECK_EQ(selForm, 0x1234);
    CHECK_EQ(hideForm, 0x1234);
    CHECK(token == "$C");
    CHECK_EQ(stopped, 77);
    CHECK_EQ(c.voiceHandle, 0);     // cleared
}

TEST(Cutscene2ReconTutorial, VoiceNotPlayingStillClearsHandleNoStop) {
    TutorialController c; c.panelForm = 5; c.voiceHandle = 88;
    TutorialHooks h; bool stopCalled = false;
    h.selectWindow=[&](guild::i32){}; h.renderRichString=[&](const char*){};
    h.setObjectsVisible=[&](guild::i32){};
    h.voiceIsPlaying=[&](guild::i32){ return false; };
    h.stopVoice=[&](guild::i32){ stopCalled = true; };
    Tutorial_HideDonePanelAndStopVoice(c, h);
    CHECK(!stopCalled);
    CHECK_EQ(c.voiceHandle, 0);     // still cleared (inside the +0x10 != 0 branch)
}

TEST(Cutscene2ReconTutorial, NoVoiceHandleSkipsAudioBranch) {
    TutorialController c; c.panelForm = 5; c.voiceHandle = 0;
    TutorialHooks h; bool playingChecked = false;
    h.selectWindow=[&](guild::i32){}; h.renderRichString=[&](const char*){};
    h.setObjectsVisible=[&](guild::i32){};
    h.voiceIsPlaying=[&](guild::i32){ playingChecked = true; return true; };
    h.stopVoice=[&](guild::i32){};
    Tutorial_HideDonePanelAndStopVoice(c, h);
    CHECK(!playingChecked);         // returned at 0x5976DA before the audio gate
    CHECK_EQ(c.voiceHandle, 0);
}

// ===========================================================================
// Theatre — VIBE_Theatre_RunEventMenu (deterministic core)
// ===========================================================================

static u16 SeqRand(std::vector<u32>& out, std::vector<u32>& seq, u32 n) {
    out.push_back(n);
    if (seq.empty()) return 0;
    u32 v = seq.front(); seq.erase(seq.begin());
    return static_cast<u16>(v % n);
}

TEST(Cutscene2ReconTheatre, EventCodesMatchDecompile) {
    CHECK_EQ((int)kTheatreCodeExecution, 5);
    CHECK_EQ((int)kTheatreCodeWedding, 6);
    CHECK_EQ((int)kTheatreCodeBirth, 7);
    CHECK_EQ((int)kTheatreCodeDeath, 8);
    CHECK_EQ((int)kTheatreCodeBankruptcy, 9);
    CHECK_EQ((int)kTheatreCodeTenancy, 10);
    CHECK_EQ((int)kTheatreCodeDuel, 4);
    CHECK_EQ((int)kTheatreCodePlague, 89);
    CHECK_EQ((int)kTheatreCodeFire, 78);
    CHECK_EQ((int)kTheatreCodeStorm, 80);
}

TEST(Cutscene2ReconTheatre, WeddingRecord) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreCutsceneRecord rec;
    bool ok = Theatre_BuildCutsceneRecord(TheatreEvent::Wedding, 3, 0xAA, 0xBB, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 6);
    CHECK_EQ((int)rec.count, 2);
    CHECK_EQ((int)rec.subKind, 32);
    CHECK_EQ(rec.targetId, 0xBB);   // partner from query
    CHECK_EQ(rec.master, 0xAA);
    // 4-arg form defaults v44 (master2) to masterDword.
    CHECK_EQ(rec.master2, 0xAA);
}

// wave-16 1:1 fix: wedding's v44 is dword_12CE914[134*localMaster + 402] — a SEPARATE
// person record's master, not the player's master. The 6-arg form carries it.
TEST(Cutscene2ReconTheatre, WeddingMaster2IsPartnerMasterSlot) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreCutsceneRecord rec;
    bool ok = Theatre_BuildCutsceneRecord(TheatreEvent::Wedding, 3, 0xAA, 0xBB, h, rec,
                                          /*weddingPartnerMaster*/ 0xCC);
    CHECK(ok);
    CHECK_EQ(rec.master,  0xAA);   // v43 = local master
    CHECK_EQ(rec.master2, 0xCC);   // v44 = +402 slot, distinct from the player's master
    CHECK_EQ(rec.targetId, 0xBB);
}

TEST(Cutscene2ReconTheatre, TenancyRecordHasAmountAndSubKind34) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreCutsceneRecord rec;
    bool ok = Theatre_BuildCutsceneRecord(TheatreEvent::Tenancy, 0, 0x10, 0xEE, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 10);
    CHECK_EQ((int)rec.subKind, 34);
    CHECK_EQ(rec.amount, 16000);
    CHECK_EQ(rec.extra, 0xEE);
}

TEST(Cutscene2ReconTheatre, DuelRecordSubKind34Count2) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreCutsceneRecord rec;
    bool ok = Theatre_BuildCutsceneRecord(TheatreEvent::Duel, 0, 0x20, 0, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 4);
    CHECK_EQ((int)rec.subKind, 34);
    CHECK_EQ((int)rec.count, 2);
}

TEST(Cutscene2ReconTheatre, DisasterEventsRejectedByCutsceneBuilder) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreCutsceneRecord rec;
    CHECK(!Theatre_BuildCutsceneRecord(TheatreEvent::Plague, 0, 0, 0, h, rec));
    CHECK(!Theatre_BuildCutsceneRecord(TheatreEvent::Fire, 0, 0, 0, h, rec));
    CHECK(!Theatre_BuildCutsceneRecord(TheatreEvent::Storm, 0, 0, 0, h, rec));
}

TEST(Cutscene2ReconTheatre, PlagueRecord) {
    std::vector<u32> calls, seq = {1 /*rand3 ->1*/, 0 /*rand2->0 days*/, 2 /*rand4->2 months*/};
    TheatreHooks h; h.randMod = [&](guild::u32 n){ return SeqRand(calls, seq, n); };
    TheatreDisasterRecord rec;
    bool ok = Theatre_BuildDisasterRecord(TheatreEvent::Plague, 0x99, /*month*/0, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 89);
    CHECK_EQ((int)rec.subKind, 2);
    CHECK_EQ(rec.srcId, 0x99);
    CHECK_EQ(rec.v57, 3);             // 3 * rand(3)==1
    CHECK_EQ(rec.advanceDays, 0);     // 30 * rand(2)==0
    CHECK_EQ(rec.advanceMonths, 2);   // rand(4)==2, month<0x17 -> no +7
}

TEST(Cutscene2ReconTheatre, FireRecord) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreDisasterRecord rec;
    bool ok = Theatre_BuildDisasterRecord(TheatreEvent::Fire, 0x11, 0, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 78);
    CHECK_EQ((int)rec.subKind, 1);
    CHECK_EQ(rec.v57, -1);
    CHECK_EQ(rec.v58, 2000);
}

TEST(Cutscene2ReconTheatre, StormRecord) {
    TheatreHooks h; h.randMod = [](guild::u32){ return (guild::u16)0; };
    TheatreDisasterRecord rec;
    bool ok = Theatre_BuildDisasterRecord(TheatreEvent::Storm, 0x22, 0, h, rec);
    CHECK(ok);
    CHECK_EQ((int)rec.kind, 80);
    CHECK_EQ((int)rec.subKind, 2);
    CHECK_EQ(rec.v58, 1000);
}

TEST(Cutscene2ReconTheatre, DisasterOffsetPushedPastMonth0x17) {
    std::vector<u32> calls, seq = {1 /*rand2->1 days=30*/, 3 /*rand4->3 months*/};
    TheatreHooks h; h.randMod = [&](guild::u32 n){ return SeqRand(calls, seq, n); };
    guild::i32 days = 0, months = 0;
    Theatre_ComputeDisasterOffset(h, 0x17, days, months);  // >= 0x17 -> +7
    CHECK_EQ(days, 30);
    CHECK_EQ(months, 10);   // 3 + 7
}

TEST(Cutscene2ReconTheatre, DisasterOffsetEarlyMonthNoBonus) {
    std::vector<u32> calls, seq = {0 /*days 0*/, 1 /*months 1*/};
    TheatreHooks h; h.randMod = [&](guild::u32 n){ return SeqRand(calls, seq, n); };
    guild::i32 days = 0, months = 0;
    Theatre_ComputeDisasterOffset(h, 0x16, days, months);
    CHECK_EQ(days, 0);
    CHECK_EQ(months, 1);
}

TEST(Cutscene2ReconTheatre, GatherTenancyPicksRoles4567) {
    // roles per record; only 4,5,6,7 are picked.
    std::vector<guild::u8> roles = {4, 1, 5, 2, 6, 3, 7, 0, 4};
    std::vector<guild::i32> ids  = {100,101,102,103,104,105,106,107,108};
    int count = 0;
    auto picked = Theatre_GatherTenancyParticipants(roles, ids, count);
    CHECK_EQ(count, 5);
    CHECK_EQ((int)picked.size(), 5);
    CHECK_EQ(picked[0], 100);
    CHECK_EQ(picked[1], 102);
    CHECK_EQ(picked[2], 104);
    CHECK_EQ(picked[3], 106);
    CHECK_EQ(picked[4], 108);
}

TEST(Cutscene2ReconTheatre, GatherTenancyCapsAtEightSlots) {
    // v21 caps at < 32 -> 8 four-byte slots.
    std::vector<guild::u8> roles(20, 4);
    std::vector<guild::i32> ids; for (int i = 0; i < 20; ++i) ids.push_back(i);
    int count = 0;
    auto picked = Theatre_GatherTenancyParticipants(roles, ids, count);
    CHECK_EQ(count, 8);          // v21 reaches 32 after 8 picks
    CHECK_EQ((int)picked.size(), 8);
}

TEST(Cutscene2ReconTheatre, GatherDuelExcludesLocalMasterAndStartsCount1) {
    std::vector<guild::i32> ids = {7, 10, 11, 12};   // 7 == localMaster -> excluded
    std::vector<guild::u8> roles = {1, 1, 1, 1};
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, /*localMaster*/7, count);
    // Decompile 0x5372f7: v29 starts at 4, +4 per pick, loop bound `v29 < 8` — so after
    // the FIRST valid (non-master) pick v29==8 and the do/while exits. Exactly ONE
    // participant is gathered (skipped records don't advance v29); count 1 -> 2.
    CHECK_EQ((int)picked.size(), 1);
    CHECK_EQ(count, 2);
    CHECK_EQ(picked[0], 10);     // 7 (localMaster) skipped, 10 is the first valid pick
}

TEST(Cutscene2ReconTheatre, GatherDuelSkipsMinusOneAndZeroRole) {
    std::vector<guild::i32> ids = {-1, 20, 21};
    std::vector<guild::u8> roles = {1, 0, 1};   // id 20 has role 0 -> skipped
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, /*localMaster*/99, count);
    CHECK_EQ((int)picked.size(), 1);
    CHECK_EQ(picked[0], 21);
    CHECK_EQ(count, 2);
}

TEST(Cutscene2ReconTheatre, FindByRoleFirstMatchAndFallback) {
    std::vector<guild::u8> roles = {13, 2, 15, 14};
    std::vector<guild::i32> dwords = {1000, 1001, 1002, 1003};
    CHECK_EQ(Theatre_FindByRole(roles, dwords, 13, -7), 1000);  // index 0 fast path
    CHECK_EQ(Theatre_FindByRole(roles, dwords, 15, -7), 1002);
    CHECK_EQ(Theatre_FindByRole(roles, dwords, 14, -7), 1003);
    CHECK_EQ(Theatre_FindByRole(roles, dwords, 99, -7), -7);    // none -> fallback
}

// ===========================================================================
// HARDENING (wave-12): theatre participant gathering over MALFORMED / mismatched
// frame data (the role/id columns are the cutscene's per-record frame table).
// Empty tables, mismatched column lengths, and oversized tables must never read a
// column out of bounds — every access is index-checked against the vector size.
// ===========================================================================
TEST(Cutscene2ReconTheatre, GatherTenancyEmptyTables) {
    std::vector<guild::u8> roles;        // empty role column
    std::vector<guild::i32> ids;         // empty id column
    int count = -1;
    auto picked = Theatre_GatherTenancyParticipants(roles, ids, count);
    CHECK_EQ(count, 0);
    CHECK(picked.empty());
}

TEST(Cutscene2ReconTheatre, GatherTenancyMismatchedColumns) {
    // More role records than id records -> the ids beyond idCol read as 0, never OOB.
    std::vector<guild::u8> roles = {4, 5, 6, 7};
    std::vector<guild::i32> ids  = {100};            // shorter than roles
    int count = 0;
    auto picked = Theatre_GatherTenancyParticipants(roles, ids, count);
    CHECK_EQ(count, 4);
    CHECK_EQ((int)picked.size(), 4);
    CHECK_EQ(picked[0], 100);
    CHECK_EQ(picked[1], 0);   // beyond idCol -> 0 (bounded read), not an OOB
    CHECK_EQ(picked[2], 0);
    CHECK_EQ(picked[3], 0);
}

TEST(Cutscene2ReconTheatre, GatherDuelMismatchedColumns) {
    // id column longer than role column -> the roles beyond roleBytes read as 0.
    std::vector<guild::i32> ids  = {10, 11, 12};
    std::vector<guild::u8> roles;                    // empty -> every role reads 0
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, /*localMaster*/99, count);
    // role 0 -> all skipped; nothing gathered; count stays 1; no OOB.
    CHECK(picked.empty());
    CHECK_EQ(count, 1);
}

TEST(Cutscene2ReconTheatre, GatherDuelOversizedTablesCapNoOOB) {
    // A table far larger than the original's 768-record scan bound; the loop caps at
    // 768 records AND at the do/while v29<8 exit — neither over-reads the vectors.
    std::vector<guild::i32> ids(2000);
    std::vector<guild::u8> roles(2000, 1);
    for (size_t i = 0; i < ids.size(); ++i) ids[i] = (guild::i32)(1000 + i);
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, /*localMaster*/-1, count);
    // v29 starts 4, +4 per pick, bound v29<8 -> exactly ONE pick before exit.
    CHECK_EQ((int)picked.size(), 1);
    CHECK_EQ(count, 2);
}

TEST(Cutscene2ReconTheatre, FindByRoleEmptyAndShortDwords) {
    std::vector<guild::u8> roles;                    // empty -> fallback
    std::vector<guild::i32> dwords;
    CHECK_EQ(Theatre_FindByRole(roles, dwords, 4, -99), -99);
    // role found but dwords column shorter than roles -> fallback for that index.
    std::vector<guild::u8> roles2 = {1, 4};
    std::vector<guild::i32> dwords1 = {500};         // index 1 missing
    CHECK_EQ(Theatre_FindByRole(roles2, dwords1, 4, -99), -99);
}

// wave-16 1:1 fix: the duel gather COMPARES the id-word column (word_12CE910) but
// STORES the master-dword column (dword_12CE914) — two distinct tables. The 5-arg
// form pins that: the gate uses `ids`, the pushed value comes from `masters`.
TEST(Cutscene2ReconTheatre, GatherDuelStoresMasterColumnNotIdColumn) {
    std::vector<guild::i32> ids     = {7, 10, 11};    // compare key (7 == localMaster)
    std::vector<guild::u8>  roles   = {1, 1, 1};
    std::vector<guild::i32> masters = {700, 1000, 1100}; // stored value column
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, masters, /*localMaster*/7, count);
    // 7 (id) == localMaster -> skipped; first valid is id 10 -> stores masters[1]==1000.
    // v29 4->8 after one pick -> exactly one gathered; count 1 -> 2.
    CHECK_EQ((int)picked.size(), 1);
    CHECK_EQ(count, 2);
    CHECK_EQ(picked[0], 1000);   // master column value, NOT the id-word 10
}

// The master column shorter than the id column reads 0 past its end (bounded), no OOB.
TEST(Cutscene2ReconTheatre, GatherDuelMasterColumnShortBoundedZero) {
    std::vector<guild::i32> ids     = {10, 11};
    std::vector<guild::u8>  roles   = {1, 1};
    std::vector<guild::i32> masters;                 // empty -> stored value reads 0
    int count = 0;
    auto picked = Theatre_GatherDuelParticipants(ids, roles, masters, /*localMaster*/-1, count);
    CHECK_EQ((int)picked.size(), 1);  // one pick before v29>=8
    CHECK_EQ(picked[0], 0);           // bounded read past the master column -> 0
    CHECK_EQ(count, 2);
}
