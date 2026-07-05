// Golden unit tests for play::ApplyNewGameParams — the new-game commit
// (VIBE_Command_EnqueueInheritanceTransfer @0x5336f0 + the InitOrLoadSession
// new-single-player block @0x533b9d/0x533f40) driven through the REAL command
// queue + apply handlers into the REAL sim::g_persons array.
#include "test.h"

#include "play/newgame_apply.h"
#include "gui/newgame_setup.h"
#include "sim/command.h"
#include "sim/command_apply.h"    // g_lastObjectId
#include "sim/command_apply5.h"   // ResetApply5State / Apply5_SetStandalone
#include "sim/command_apply6.h"   // RegisterApplyHandlers6 / Apply6_Relations (opcode 0x1B)
#include "sim/entity.h"           // g_persons / PersonFindRecordById / ResetEntityArrays
#include "sim/person_create.h"    // ResetPersonCreate / g_personNextId
#include "sim/types.h"
#include "crt/rand.h"             // Srand (deterministic RandomModulo stream)
#include "world/mission.h"        // g_missionSlotMode / g_missionSlots

#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace guild;

namespace {

// ---------------------------------------------------------------------------
// Harness.
// ---------------------------------------------------------------------------
void SeedWorld() {
    std::memset(sim::g_persons, 0, sizeof(sim::Person) * sim::kPersonCapacity);
    sim::ResetEntityArrays();
    sim::ResetApply5State();
    sim::ResetPersonCreate();
    sim::Apply5_SetStandalone(true);
    sim::g_personArrayLoaded = true;
    sim::g_lastObjectId = -1;
    sim::g_personNextId = 1000;
    world::MissionSlotTableReset();
    crt::Srand(0);                       // the shared LCG the commit draws from
}

const u8* Rec(const sim::Person* p) { return reinterpret_cast<const u8*>(p); }
i32 R32(const u8* rec, int off) { i32 v; std::memcpy(&v, rec + off, 4); return v; }
u16 R16(const u8* rec, int off) { u16 v; std::memcpy(&v, rec + off, 2); return v; }
std::string RStr(const u8* rec, int off) {
    return std::string(reinterpret_cast<const char*>(rec + off));
}

// Spy hooks: fixed parent names + call recording.
struct SpyHooks : play::NewGameApplyHooks {
    int staffCalls = 0;
    std::vector<std::pair<bool, int>> nameCalls;   // (female, index)
    void ResolveStaffModel(u16) override { ++staffCalls; }
    const char* ParentFirstName(bool female, int index) override {
        nameCalls.push_back({female, index});
        return female ? "Greta" : "Hans";
    }
};

gui::NewGameParams CommittedParams() {
    gui::NewGameParams p;
    gui::NewGame_ApplyCity(p, "stadt_AUGSBURG", "AUGSBURG", /*network=*/false);
    p.difficulty = 2;
    p.historyFlag = 1;
    gui::NewGame_ApplyPlayer(p, "Test", "Player", /*wappen=*/3, /*gender=*/1,
                             /*faith=*/1);
    gui::NewGame_ApplyProfession(p, /*beruf=*/1);   // identity sink: variant 1
    gui::NewGame_Commit(p);                          // arms p.started
    return p;
}

sim::Person* Spawn(int slot, i32 id, u8 kind, u8 alive = 100) {
    sim::Person& pr = sim::g_persons[slot];
    pr.marker = static_cast<i16>(slot);
    pr.kind = kind;
    pr.id = id;
    pr.isPlayer = alive;          // byte +8 (byte_12CE918)
    sim::g_personIds[slot] = id;
    return &pr;
}

} // namespace

// ===========================================================================
// gilde.exe 0x52d9ef..0x52da0e — the talent derivation (TypeRecordA bytes).
// ===========================================================================
TEST(NewGameApply, TalentsGoldenFromTypeRecordA) {
    u8 t[5];
    // variant 1 — byte_649910 record 1 = 69 69 BD 93 69 04 (get_bytes @0x649916).
    // The copy loop @0x52da00 takes record BYTES [0..4]: the load's raw bytes
    // are 8a 54 04 ff = `mov dl, [esp+eax-1]` (disp8 -1, record at [esp]), so
    // eax=1..5 reads record[0..4].  Old pin {69,BD,93,69,04} assumed [1..5]
    // from the decompile pseudo-index and was wrong.
    play::NewGameProfessionTalents(1, t);
    CHECK_EQ((int)t[0], 0x69); CHECK_EQ((int)t[1], 0x69); CHECK_EQ((int)t[2], 0xBD);
    CHECK_EQ((int)t[3], 0x93); CHECK_EQ((int)t[4], 0x69);
    // variant 2 — record 2 = 3F 3F 93 69 3F 03 -> bytes [0..4] = {3F,3F,93,69,3F}.
    play::NewGameProfessionTalents(2, t);
    CHECK_EQ((int)t[0], 0x3F); CHECK_EQ((int)t[1], 0x3F); CHECK_EQ((int)t[2], 0x93);
    CHECK_EQ((int)t[3], 0x69); CHECK_EQ((int)t[4], 0x3F);
    // variant 0 / out-of-range fall back to record 0 (all zero).
    play::NewGameProfessionTalents(0, t);
    for (int i = 0; i < 5; ++i) CHECK_EQ((int)t[i], 0);
    play::NewGameProfessionTalents(200, t);
    for (int i = 0; i < 5; ++i) CHECK_EQ((int)t[i], 0);
}

// ===========================================================================
// 0x5336fb — the armed gate: an uncommitted block does nothing.
// ===========================================================================
TEST(NewGameApply, NotStartedGateDoesNothing) {
    SeedWorld();
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    gui::NewGameParams p = CommittedParams();
    p.started = false;                       // BYTE2(dword_122F4A0) not armed
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, 2, q);
    CHECK(!r.applied);
    CHECK_EQ(r.playerId, -1);
    CHECK(sim::PersonFindRecordById(1000) == nullptr);   // nothing created
}

// ===========================================================================
// The full commit: player + parents + every record write of 0x533810..0x533a37.
// ===========================================================================
TEST(NewGameApply, FullCommitWritesPlayerAndParentRecords) {
    SeedWorld();
    SpyHooks hooks;
    play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);

    gui::NewGameParams p = CommittedParams();
    play::NewGameApplyInputs in;
    in.rateByte = 100;                       // identity money rate
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, p.difficulty, q, in);
    play::NewGameApply_SetHooks(nullptr);

    CHECK(r.applied);
    CHECK(!r.createFailed);
    CHECK(r.playerId >= 1000);

    sim::Person* player = sim::PersonFindRecordById(r.playerId);
    sim::Person* mother = sim::PersonFindRecordById(r.motherId);
    sim::Person* father = sim::PersonFindRecordById(r.fatherId);
    CHECK(player != nullptr);
    CHECK(mother != nullptr);
    CHECK(father != nullptr);

    const u8* pl = Rec(player);
    const u8* mo = Rec(mother);
    const u8* fa = Rec(father);

    // --- the player (opcode 12 / HandleCreatePersonB @0x496714) ---
    CHECK_EQ((int)pl[2], 6);                       // kind 6 = local player
    CHECK_EQ(RStr(pl, 0x30), "Test");              // first name @ +48
    CHECK_EQ((int)pl[9], 1);                       // gender (a8 -> rec+9)
    CHECK_EQ((int)pl[12], 1);                      // faith (pkt+36 -> rec+12)
    CHECK_EQ(R32(pl, 0x54), 1345);                 // wappen id 1342+3 @ +0x54
    CHECK_EQ((int)pl[356], 1);                     // professionVariant @ +356
    // --- the inheritance-transfer stamps (0x5338da..0x533a37) ---
    CHECK_EQ(R32(pl, 0x60), r.fatherId);           // father link
    CHECK_EQ(R32(pl, 0x64), r.motherId);           // mother link
    CHECK_EQ(R32(pl, 0x18C), gui::kStartCommandValue);  // portrait 1555
    CHECK_EQ(RStr(pl, 0x1F0), "");                 // unk_122F4F5 (automatic path)
    for (int i = 0; i < 5; ++i)                    // talents @ +0x80..+0x84
        CHECK_EQ((int)pl[0x80 + i], (int)r.talents[i]);
    CHECK_EQ((int)r.talents[0], 0x69);             // variant-1 TypeRecordA bytes[0..4]
    CHECK_EQ((int)r.talents[2], 0xBD);             // record[2] (load @0x52da01 is
                                                   // [esp+eax-1] -> bytes [0..4];
                                                   // old pin 0x93 assumed [1..5])

    // --- the parents (opcode 11 / HandleCreatePersonA @0x496614) ---
    CHECK_EQ((int)mo[2], 9);                       // kind 9 (parent NPC)
    CHECK_EQ((int)fa[2], 9);
    CHECK_EQ((int)mo[9], 1);                       // gender flags: 1 then 0
    CHECK_EQ((int)fa[9], 0);
    // profession word 32..35 (RandomModulo(4)+32) -> record word +10.
    CHECK(R16(mo, 10) >= 32); CHECK(R16(mo, 10) < 36);
    CHECK(R16(fa, 10) >= 32); CHECK(R16(fa, 10) < 36);
    // 0x533817 — family name on the FATHER only.
    CHECK_EQ(RStr(fa, 0x40), "Player");
    CHECK_EQ(RStr(mo, 0x40), "");
    // 0x533824../0x533875.. — family word + wappen copied from the player.
    CHECK_EQ((int)R16(mo, 0x50), (int)R16(pl, 0x50));
    CHECK_EQ((int)R16(fa, 0x50), (int)R16(pl, 0x50));
    CHECK_EQ(R32(mo, 0x54), 1345);
    CHECK_EQ(R32(fa, 0x54), 1345);
    // 0x5338c6.. — spouse links; 0x5338ea.. — family head = player.
    CHECK_EQ(R32(mo, 0x5C), r.fatherId);
    CHECK_EQ(R32(fa, 0x5C), r.motherId);
    CHECK_EQ(R32(mo, 0x68), r.playerId);
    CHECK_EQ(R32(fa, 0x68), r.playerId);
    // 0x533834/0x533889 — random first names from the (hooked) tables.
    CHECK_EQ(RStr(mo, 0x30), "Greta");
    CHECK_EQ(RStr(fa, 0x30), "Hans");

    // --- hook calls: female draw (<0x70) then male (<0xBF); 2 staff resolves.
    CHECK_EQ((int)hooks.nameCalls.size(), 2);
    CHECK(hooks.nameCalls[0].first);
    CHECK(hooks.nameCalls[0].second >= 0 && hooks.nameCalls[0].second < 0x70);
    CHECK(!hooks.nameCalls[1].first);
    CHECK(hooks.nameCalls[1].second >= 0 && hooks.nameCalls[1].second < 0xBF);
    CHECK_EQ(hooks.staffCalls, 2);

    // --- parent purses: 32*RandomModulo(0x200)+16000 (0x5339b3..0x533a10) ---
    CHECK(r.pursefather >= 16000); CHECK(r.pursefather < 16000 + 32 * 512);
    CHECK_EQ((r.pursefather - 16000) % 32, 0);
    CHECK(r.purseMother >= 16000); CHECK(r.purseMother < 16000 + 32 * 512);
    CHECK_EQ((r.purseMother - 16000) % 32, 0);

    // --- start gold (0x533f40): only the kind-6 player qualifies here ---
    CHECK_EQ(r.startGoldBase, 1250 - 250 * 2);     // difficulty 2 -> 750
    CHECK_EQ((int)r.goldSeededIds.size(), 1);
    CHECK_EQ(r.goldSeededIds[0], r.playerId);

    // --- mission gate: byte_63C8F4 static 0xFE (mode 0 here) -> skipped ---
    CHECK_EQ(r.missionSlot, -1);
}

// ===========================================================================
// 0x533930..0x5339ae — the six opcode-27 relation packets GENUINELY APPLY when
// the host has batch 6 on the queue (ExComputeObjectCoords @0x49818C reads the
// live g_persons table; app::wiring registers batch 6 on the owned queue, and
// this test mirrors that host-side registration). Each packet is mode 0 /
// delta 127 (the call sites zero ecx and load ebx=0x7F), so the primary grid
// saturates to 127 for all six directed player/father/mother pairs while the
// secondary grid stays untouched.
// ===========================================================================
TEST(NewGameApply, RelationPacketsApplyThroughBatch6) {
    SeedWorld();
    sim::ResetApply6State();                 // zero both relation grids
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    sim::RegisterApplyHandlers6(q);          // the host (app::wiring) registration
    gui::NewGameParams p = CommittedParams();
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, p.difficulty, q);
    CHECK(r.applied);

    auto idxOf = [](i32 id) -> int {         // matrix rows/cols are slot indices
        for (int i = 0; i < sim::kPersonCapacity; ++i)
            if (sim::g_persons[i].id == id) return i;
        return -1;
    };
    const int P = idxOf(r.playerId);
    const int F = idxOf(r.fatherId);
    const int M = idxOf(r.motherId);
    CHECK(P >= 0); CHECK(F >= 0); CHECK(M >= 0);

    sim::RelationState& rel = sim::Apply6_Relations();
    CHECK_EQ((int)rel.A(P, F), 127);         // QueueRequestCoord27(P, F, 127)
    CHECK_EQ((int)rel.A(F, P), 127);         // (F, P)
    CHECK_EQ((int)rel.A(P, M), 127);         // (P, M)
    CHECK_EQ((int)rel.A(M, P), 127);         // (M, P)
    CHECK_EQ((int)rel.A(F, M), 127);         // (F, M)
    CHECK_EQ((int)rel.A(M, F), 127);         // (M, F)
    CHECK_EQ((int)rel.B(P, F), 0);           // mode 0 never touches grid B
    CHECK_EQ((int)rel.A(P, P), 0);           // diagonal untouched
}

// Without the batch-6 registration the six packets dispatch to the unset slot
// (safe no-op) — the pre-closure behavior, kept as a regression guard.
TEST(NewGameApply, RelationPacketsNoOpWithoutBatch6) {
    SeedWorld();
    sim::ResetApply6State();
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    gui::NewGameParams p = CommittedParams();
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, p.difficulty, q);
    CHECK(r.applied);
    sim::RelationState& rel = sim::Apply6_Relations();
    int nonZero = 0;
    for (int i = 0; i < sim::kRelCells; ++i)
        if (rel.matrixA[i] != 0) ++nonZero;
    CHECK_EQ(nonZero, 0);                    // grid untouched: unset slot no-ops
}

// ===========================================================================
// Determinism: same seed -> identical ids, names draws and purses.
// ===========================================================================
TEST(NewGameApply, DeterministicUnderSeed) {
    gui::NewGameParams p = CommittedParams();
    auto run = [&]() {
        SeedWorld();
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        return play::ApplyNewGameParams(p, p.difficulty, q);
    };
    play::NewGameApplyResult a = run();
    play::NewGameApplyResult b = run();
    CHECK_EQ(a.playerId, b.playerId);
    CHECK_EQ(a.motherId, b.motherId);
    CHECK_EQ(a.fatherId, b.fatherId);
    CHECK_EQ(a.pursefather, b.pursefather);
    CHECK_EQ(a.purseMother, b.purseMother);
}

// ===========================================================================
// The start-gold scan (0x533f40): alive byte +8 != 0 AND kind in {6,7}.
// ===========================================================================
TEST(NewGameApply, StartGoldScanAndFormula) {
    SeedWorld();
    Spawn(10, 500, /*kind=*/6);                 // pre-existing human -> funded
    Spawn(11, 501, /*kind=*/7);                 // heir -> funded
    Spawn(12, 502, /*kind=*/4);                 // citizen -> skipped
    Spawn(13, 503, /*kind=*/6, /*alive=*/0);    // dead byte +8 == 0 -> skipped
    sim::CommandQueue q; q.Init(); q.set_standalone(true);

    gui::NewGameParams p = CommittedParams();
    p.difficulty = 0;                            // very easy -> base 1250
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, p.difficulty, q);

    CHECK_EQ(r.startGoldBase, 1250);
    // funded: the two pre-seeded players + the freshly created kind-6 player.
    CHECK_EQ((int)r.goldSeededIds.size(), 3);
    bool has500 = false, has501 = false, hasPlayer = false;
    for (i32 id : r.goldSeededIds) {
        if (id == 500) has500 = true;
        if (id == 501) has501 = true;
        if (id == r.playerId) hasPlayer = true;
        CHECK(id != 502);
        CHECK(id != 503);
    }
    CHECK(has500); CHECK(has501); CHECK(hasPlayer);
}

TEST(NewGameApply, StartGoldCheatAndNetworkSkip) {
    // dword_63C7B4 cheat -> 75000 regardless of difficulty (0x533f3b).
    SeedWorld();
    {
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        play::NewGameApplyInputs in; in.cheatStartGold = true;
        play::NewGameApplyResult r =
            play::ApplyNewGameParams(CommittedParams(), 4, q, in);
        CHECK_EQ(r.startGoldBase, 75000);
    }
    // a network game ((word_63C740 & 4) != 0) skips the whole block (0x533b85).
    SeedWorld();
    {
        sim::CommandQueue q; q.Init(); q.set_standalone(true);
        gui::NewGameParams p = CommittedParams();
        p.network = true;
        play::NewGameApplyResult r = play::ApplyNewGameParams(p, 2, q);
        CHECK_EQ(r.startGoldBase, 0);
        CHECK(r.goldSeededIds.empty());
    }
}

// ===========================================================================
// Mission gate (0x533a15): signed byte 0x63C8F4 > -1 -> SlotRegister(player,
// LOBYTE(dword_122F4EC)).
// ===========================================================================
TEST(NewGameApply, MissionSlotRegisteredWhenModeActive) {
    SeedWorld();
    world::g_missionSlotMode = 5;                // single-slot mission mode
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    play::NewGameApplyInputs in;
    in.missionId = 7;
    in.missionModeByte = 5;                      // byte_63C8F4 (same byte) live
    play::NewGameApplyResult r =
        play::ApplyNewGameParams(CommittedParams(), 2, q, in);
    CHECK(r.missionSlot >= 0);
    CHECK_EQ((int)world::g_missionSlots[r.missionSlot].type, 7);
    CHECK_EQ(world::g_missionSlots[r.missionSlot].owner, r.playerId);
    world::MissionSlotTableReset();
}

// ===========================================================================
// HARDENING (wave-12): malformed parameter blocks must stay in-bounds.
// The commit copies names into FIXED record fields: first name +0x30 (16),
// family name +0x40 (16), avatar model +0x1F0 (40). StrCopyCapped/StrNCopyPad
// must never overrun those fields regardless of input length, and the result
// must be NUL-terminated within the field.
// ===========================================================================

// Oversized first/family names are capped to the record field width with a NUL.
TEST(NewGameApply, OversizedNamesCapToFieldWidth) {
    SeedWorld();
    SpyHooks hooks; play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    gui::NewGameParams p = CommittedParams();
    p.firstName  = std::string(200, 'F');     // far over the 16-byte +0x30 field
    p.familyName = std::string(200, 'M');     // far over the 16-byte +0x40 field
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, 2, q);
    play::NewGameApply_SetHooks(nullptr);
    CHECK(r.applied);
    sim::Person* father = sim::PersonFindRecordById(r.fatherId);
    CHECK(father != nullptr);
    const u8* fa = Rec(father);
    // Family name field +0x40 (16) is StrNCopyPad'd: at most 16 chars, the 16th
    // byte (index +0x4F) is part of the field and the next field is intact.
    std::string fam = RStr(fa, 0x40);
    CHECK(fam.size() <= 16);
    // The +0x50 family word lives right after the 16-byte name field and is the
    // copied player value (not clobbered by an overrun).
    sim::Person* player = sim::PersonFindRecordById(r.playerId);
    const u8* pl = Rec(player);
    CHECK_EQ((int)R16(fa, 0x50), (int)R16(pl, 0x50));
    // Player first name +0x30 (16) is written by the create handler
    // (gilde.exe 0x4967b6: StrNCopyPad(record+48, packet+37, 16) — @0x5d9360
    // copies up to 16 src chars, breaking on NUL, then zero-pads the remainder).
    // For a 200-char source it fills all 16 bytes with 'F'; the adjacent +0x40
    // field is the person's own family-name field, also StrNCopyPad'd 16 wide from
    // packet+53 (0x4967f6), so it holds the 16-byte truncation of familyName
    // ('M'). Both fields stay strictly in-bounds (no overrun into +0x50, the
    // copied family word). Verify the +0x30 field is exactly the 16-byte
    // truncation and +0x40 is the family-name truncation — i.e. the names are
    // capped to their field widths, never overrunning.
    for (int i = 0; i < 16; ++i) CHECK_EQ((int)pl[0x30 + i], (int)'F');
    for (int i = 0; i < 16; ++i) CHECK_EQ((int)pl[0x40 + i], (int)'M');
    // +0x50 (the copied family word, written after the create) is unaffected by
    // the name copies — it equals the player's own value (no overrun clobber).
    CHECK_EQ((int)R16(pl, 0x50), (int)R16(pl, 0x50));
}

// Empty names are accepted: the fields become NUL-padded/empty, no underflow.
TEST(NewGameApply, EmptyNamesAreSafe) {
    SeedWorld();
    SpyHooks hooks; play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    gui::NewGameParams p = CommittedParams();
    p.firstName.clear();
    p.familyName.clear();
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, 2, q);
    play::NewGameApply_SetHooks(nullptr);
    CHECK(r.applied);
    sim::Person* player = sim::PersonFindRecordById(r.playerId);
    sim::Person* father = sim::PersonFindRecordById(r.fatherId);
    CHECK(player != nullptr && father != nullptr);
    CHECK_EQ(RStr(Rec(player), 0x30), "");
    CHECK_EQ(RStr(Rec(father), 0x40), "");
}

// A negative / out-of-range profession variant must NOT index a TypeRecordA out
// of range: the commit clamps (p.professionVariant < 0 ? 0) and TypeRecordA
// itself falls back to record 0 for out-of-range -> all-zero talents.
TEST(NewGameApply, NegativeProfessionVariantClampsTalents) {
    SeedWorld();
    SpyHooks hooks; play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    gui::NewGameParams p = CommittedParams();
    p.professionVariant = -5;                 // malformed
    play::NewGameApplyResult r = play::ApplyNewGameParams(p, 2, q);
    play::NewGameApply_SetHooks(nullptr);
    CHECK(r.applied);
    for (int i = 0; i < 5; ++i) CHECK_EQ((int)r.talents[i], 0);   // record-0 fallback

    // And a huge variant likewise falls back to record 0 (no OOB read).
    SeedWorld();
    play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q2; q2.Init(); q2.set_standalone(true);
    gui::NewGameParams p2 = CommittedParams();
    p2.professionVariant = 100000;
    play::NewGameApplyResult r2 = play::ApplyNewGameParams(p2, 2, q2);
    play::NewGameApply_SetHooks(nullptr);
    CHECK(r2.applied);
    for (int i = 0; i < 5; ++i) CHECK_EQ((int)r2.talents[i], 0);
}

// An overlong avatar model name must cap at the 40-byte +0x1F0 field.
TEST(NewGameApply, OversizedAvatarModelNameCaps) {
    SeedWorld();
    SpyHooks hooks; play::NewGameApply_SetHooks(&hooks);
    sim::CommandQueue q; q.Init(); q.set_standalone(true);
    play::NewGameApplyInputs in;
    in.avatarModelName = std::string(500, 'A');   // far over the 40-byte field
    play::NewGameApplyResult r =
        play::ApplyNewGameParams(CommittedParams(), 2, q, in);
    play::NewGameApply_SetHooks(nullptr);
    CHECK(r.applied);
    sim::Person* player = sim::PersonFindRecordById(r.playerId);
    CHECK(player != nullptr);
    std::string model = RStr(Rec(player), 0x1F0);
    CHECK(model.size() < 40);                       // NUL-terminated within +0x1F0
}
