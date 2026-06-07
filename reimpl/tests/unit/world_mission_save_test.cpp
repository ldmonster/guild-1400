// Unit tests for the deferred mission save/load + dialog-flow + guild-member cores:
//   * VIBE_Mission_SaveSlotTable/LoadSlotTable byte-exact serialization (golden
//     layout: mode + 128 * {type,owner,deadline14,f24,f28,state} == 3585 bytes)
//   * VIBE_Mission_SlotSetSingle single-slot stamp
//   * VIBE_Mission_DialogDispatcher routing table
//   * VIBE_Mission_RunGiveDialog owned-slot scan + give-outcome decode + seed
//   * VIBE_Mission_RunAcceptDialog accept-outcome reload rule
//   * VIBE_Mission_FindGuildMemberState person-link kind scan
#include "test.h"

#include <cstring>
#include <vector>

#include "world/mission.h"
#include "world/mission_save.h"
#include "world/mission_dialog.h"
#include "world/mission_member.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// SlotSetSingle
// ---------------------------------------------------------------------------
TEST(MissionSave, SlotSetSingleStampsSlot0) {
    MissionSlotTableReset();
    // Dirty slot 0 first so we can see the deadline/state clear.
    g_missionSlots[0].fieldAt28 = 99;
    std::memset(&g_missionSlots[0].deadline, 0xCC, sizeof(g_missionSlots[0].deadline));
    g_missionSlots[0].state = 4;

    MissionSlotSetSingle(0x12345678, 7);
    CHECK_EQ(g_missionSlots[0].type, (u8)7);
    CHECK_EQ(g_missionSlots[0].owner, (i32)0x12345678);
    CHECK_EQ(g_missionSlots[0].state, (u8)0);
    // deadline block fully zeroed.
    const u8* d = reinterpret_cast<const u8*>(&g_missionSlots[0].deadline);
    int nz = 0;
    for (size_t i = 0; i < sizeof(g_missionSlots[0].deadline); ++i) nz += d[i];
    CHECK_EQ(nz, 0);
}

// ---------------------------------------------------------------------------
// Save/Load golden-vector serialization
// ---------------------------------------------------------------------------
TEST(MissionSave, SerializeLayoutGolden) {
    MissionSlotTableReset();
    g_missionSlotMode = 5;
    g_missionSlots[0].type  = 7;
    g_missionSlots[0].owner = 0x11223344;
    reinterpret_cast<u8*>(&g_missionSlots[0].deadline)[0] = 0xAB;
    g_missionSlots[0].fieldAt24 = 5;
    g_missionSlots[0].fieldAt28 = 9;
    g_missionSlots[0].state = 3;

    std::vector<u8> buf(kMissionTableSerializedBytes, 0);
    MissionStream s{buf.data(), buf.size(), 0};
    CHECK(MissionSaveSlotTable(s));
    CHECK_EQ(s.pos, kMissionTableSerializedBytes);
    CHECK_EQ((int)kMissionTableSerializedBytes, 3585);

    // Golden offsets (computed in python): mode@0, slot0 type@1, owner@2..5 LE,
    // deadline@6, f24@20, f28@24, state@28.
    CHECK_EQ(buf[0], (u8)5);
    CHECK_EQ(buf[1], (u8)7);
    CHECK_EQ(buf[2], (u8)0x44);
    CHECK_EQ(buf[3], (u8)0x33);
    CHECK_EQ(buf[4], (u8)0x22);
    CHECK_EQ(buf[5], (u8)0x11);
    CHECK_EQ(buf[6], (u8)0xAB);
    CHECK_EQ(buf[20], (u8)5);
    CHECK_EQ(buf[24], (u8)9);
    CHECK_EQ(buf[28], (u8)3);
    long sum = 0; for (u8 b : buf) sum += b;
    CHECK_EQ(sum, 370L);
}

TEST(MissionSave, SaveLoadRoundTrip) {
    MissionSlotTableReset();
    g_missionSlotMode = 5;
    // Fill several slots with distinct content.
    for (int i = 0; i < kMissionSlotCount; ++i) {
        g_missionSlots[i].type      = (u8)((i % 200) + 1);  // never 0 (occupied)
        g_missionSlots[i].owner     = 1000 + i;
        g_missionSlots[i].fieldAt24 = i * 3;
        g_missionSlots[i].fieldAt28 = i * 7;
        g_missionSlots[i].state     = (u8)(i & 0x3F);
        reinterpret_cast<u8*>(&g_missionSlots[i].deadline)[0] = (u8)(i ^ 0x5A);
        reinterpret_cast<u8*>(&g_missionSlots[i].deadline)[13] = (u8)(i + 1);
    }
    std::vector<u8> buf(kMissionTableSerializedBytes, 0);
    MissionStream w{buf.data(), buf.size(), 0};
    CHECK(MissionSaveSlotTable(w));

    // Snapshot, wipe, reload.
    std::vector<MissionSlot> snap(g_missionSlots, g_missionSlots + kMissionSlotCount);
    u8 snapMode = g_missionSlotMode;
    MissionSlotTableReset();

    MissionStream r{buf.data(), buf.size(), 0};
    CHECK(MissionLoadSlotTable(r));
    CHECK_EQ(g_missionSlotMode, snapMode);
    for (int i = 0; i < kMissionSlotCount; ++i) {
        CHECK_EQ(g_missionSlots[i].type, snap[i].type);
        CHECK_EQ(g_missionSlots[i].owner, snap[i].owner);
        CHECK_EQ(g_missionSlots[i].fieldAt24, snap[i].fieldAt24);
        CHECK_EQ(g_missionSlots[i].fieldAt28, snap[i].fieldAt28);
        CHECK_EQ(g_missionSlots[i].state, snap[i].state);
        CHECK_EQ(std::memcmp(&g_missionSlots[i].deadline, &snap[i].deadline,
                             sizeof(snap[i].deadline)), 0);
    }
}

TEST(MissionSave, SaveFailsOnShortBuffer) {
    MissionSlotTableReset();
    std::vector<u8> tiny(10, 0);
    MissionStream s{tiny.data(), tiny.size(), 0};
    CHECK(!MissionSaveSlotTable(s));   // can't even fit slot 0
}

// ---------------------------------------------------------------------------
// DialogDispatcher routing
// ---------------------------------------------------------------------------
TEST(MissionDialog, DispatcherRouting) {
    // giveFlag == -1 -> Give (regardless of the rest).
    CHECK(MissionDispatchDialog(-1, 7, 7, 5) == MissionDialogKind::kGive);
    CHECK(MissionDispatchDialog(-1, 1, 2, 0) == MissionDialogKind::kGive);
    // active == person, mode 5 -> Accept; else Offer.
    CHECK(MissionDispatchDialog(0, 7, 7, 5) == MissionDialogKind::kAccept);
    CHECK(MissionDispatchDialog(0, 7, 7, 0) == MissionDialogKind::kOffer);
    // active != person, mode 5 -> Completion; else Result.
    CHECK(MissionDispatchDialog(0, 7, 9, 5) == MissionDialogKind::kCompletion);
    CHECK(MissionDispatchDialog(0, 7, 9, 0) == MissionDialogKind::kResult);
}

// ---------------------------------------------------------------------------
// Owned-slot scan
// ---------------------------------------------------------------------------
TEST(MissionDialog, FindOwnedSlot) {
    MissionSlotTableReset();
    CHECK_EQ(MissionFindOwnedSlot(42), -1);            // empty table
    g_missionSlots[3].type  = 11;  g_missionSlots[3].owner = 42;
    g_missionSlots[7].type  = 0;   g_missionSlots[7].owner = 42;  // empty slot ignored
    g_missionSlots[9].type  = 19;  g_missionSlots[9].owner = 42;  // later dup
    CHECK_EQ(MissionFindOwnedSlot(42), 3);             // first occupied match
    CHECK_EQ(MissionFindOwnedSlot(99), -1);
}

// ---------------------------------------------------------------------------
// Give/Accept outcome decode + history seed
// ---------------------------------------------------------------------------
TEST(MissionDialog, GiveOutcomeDecode) {
    CHECK(MissionDecodeGive(0)  == MissionGiveOutcome::kDecline);
    CHECK(MissionDecodeGive(-1) == MissionGiveOutcome::kAbandon);
    CHECK(MissionDecodeGive(11) == MissionGiveOutcome::kRegister);
    CHECK(!MissionGiveTriggersReload(MissionGiveOutcome::kDecline));
    CHECK(MissionGiveTriggersReload(MissionGiveOutcome::kAbandon));
    CHECK(!MissionGiveTriggersReload(MissionGiveOutcome::kRegister));
}

TEST(MissionDialog, GiveHistorySeed) {
    CHECK_EQ(MissionGiveHistorySeed(-1), -1);   // no descriptor
    CHECK_EQ(MissionGiveHistorySeed(0), 1);     // category 0 -> seed 1
    CHECK_EQ(MissionGiveHistorySeed(4), 5);     // category 4 -> seed 5
}

TEST(MissionDialog, AcceptReloadRule) {
    CHECK(!MissionAcceptTriggersReload(MissionAcceptOutcome::kAccept));
    CHECK(!MissionAcceptTriggersReload(MissionAcceptOutcome::kDecline));
    CHECK(MissionAcceptTriggersReload(MissionAcceptOutcome::kLater));
}

// ---------------------------------------------------------------------------
// FindGuildMemberState — mock person query
// ---------------------------------------------------------------------------
namespace {
struct FakeQuery {
    std::vector<u16> linked;   // linked index per iterated person
    std::vector<u8>  kinds;    // kind byte by index
    int cursor = -1;
};
bool fq_begin(void* self, i32, u8) {
    auto* q = static_cast<FakeQuery*>(self);
    q->cursor = 0;
    return !q->linked.empty();
}
bool fq_next(void* self) {
    auto* q = static_cast<FakeQuery*>(self);
    ++q->cursor;
    return q->cursor < (int)q->linked.size();
}
u16 fq_linked(void* self) {
    auto* q = static_cast<FakeQuery*>(self);
    return q->linked[q->cursor];
}
u8 fq_kind(void* self, u16 idx) {
    auto* q = static_cast<FakeQuery*>(self);
    return (idx < q->kinds.size()) ? q->kinds[idx] : (u8)0;
}
MissionMemberQuery Wrap(FakeQuery& q) {
    MissionMemberQuery m;
    m.begin = fq_begin; m.next = fq_next;
    m.linkedIndex = fq_linked; m.kindOf = fq_kind; m.self = &q;
    return m;
}
} // namespace

TEST(MissionMember, EmptyQueryReturnsZero) {
    FakeQuery q;  // no persons
    auto m = Wrap(q);
    CHECK_EQ(MissionFindGuildMemberState(m, 0, 5), 0);
}

TEST(MissionMember, AllMembersIterateToCount) {
    FakeQuery q;
    // 3 persons, each links to index 1/2/3; all kinds 6/7 -> member states.
    q.linked = {1, 2, 3};
    q.kinds  = {0, 6, 7, 6};   // index 0 unused
    auto m = Wrap(q);
    // Each person passes; query runs out after the 3rd -> count == 3.
    CHECK_EQ(MissionFindGuildMemberState(m, 0, 5), 3);
}

TEST(MissionMember, NonMemberKindReturnsMinusOne) {
    FakeQuery q;
    q.linked = {1, 2, 3};
    q.kinds  = {0, 6, 4, 7};   // index 2 -> kind 4 (not a member state)
    auto m = Wrap(q);
    CHECK_EQ(MissionFindGuildMemberState(m, 0, 5), -1);
}

TEST(MissionMember, NoLinkReturnsMinusOne) {
    FakeQuery q;
    q.linked = {1, 0xFFFF, 3};  // 2nd person has no link
    q.kinds  = {0, 6, 7, 6};
    auto m = Wrap(q);
    CHECK_EQ(MissionFindGuildMemberState(m, 0, 5), -1);
}
