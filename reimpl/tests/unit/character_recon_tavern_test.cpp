// Golden unit tests for the one pure record/slot rule of the VIBE_Character
// cluster (gilde.exe):
//   0x4d5c60  VIBE_Character_FindTavernTargetSlot
//
// Self-contained: builds raw TavernObject / TavernTarget / slot-table fixtures and
// checks every guard and the linear-scan match/terminate behavior against the
// decompile. The turn predicate is supplied as a golden function so the scan logic
// is exercised independently of the (separately reconstructed) IsObjectForTurn.
#include "tests/framework/test.h"

#include "sim/character_recon_tavern.h"

#include <vector>

using namespace guild::sim;

namespace {

// Always-true / always-false golden turn predicates.
bool TurnYes(const TavernObject*) { return true; }
bool TurnNo(const TavernObject*) { return false; }

// Build a valid (passes all four guards) object pointing at a target with matchId.
TavernObject MakeObj(TavernTarget* tgt) {
    TavernObject o{};
    o.markerWord = 0x0001;   // != 0xFFFF
    o.typeByte = 0;          // == 0
    o.enabledByte = 1;       // != 0
    o.tavernTarget = tgt;    // != null
    return o;
}

// A 16-slot table; default all entries null (empty table).
std::vector<TavernSlot> MakeSlots() {
    std::vector<TavernSlot> s(16);
    for (auto& e : s) e.entry = nullptr;
    return s;
}

} // namespace

// ---- Guard rejections (return false, outIndex untouched) --------------------

TEST(CharacterReconTavern, RejectFreeMarker) {
    TavernTarget tgt{42};
    TavernSlotEntry e{42, 7};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    o.markerWord = 0xFFFF;  // free slot
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

TEST(CharacterReconTavern, RejectDisabled) {
    TavernTarget tgt{42};
    TavernSlotEntry e{42, 7};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    o.enabledByte = 0;  // +8 == 0
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

TEST(CharacterReconTavern, RejectNonZeroType) {
    TavernTarget tgt{42};
    TavernSlotEntry e{42, 7};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    o.typeByte = 1;  // +2 != 0
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

TEST(CharacterReconTavern, RejectNoTarget) {
    TavernSlotEntry e{42, 7};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(nullptr);  // +184 == null
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

TEST(CharacterReconTavern, RejectNotForTurn) {
    TavernTarget tgt{42};
    TavernSlotEntry e{42, 7};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    // All guards pass, but the turn predicate vetoes.
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnNo));
    CHECK_EQ(out, -999);
}

// ---- Empty table: slots[0].entry == null => false ---------------------------

TEST(CharacterReconTavern, EmptyTable) {
    TavernTarget tgt{42};
    auto slots = MakeSlots();  // all null
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

// ---- Match at index 0 -------------------------------------------------------

TEST(CharacterReconTavern, MatchFirst) {
    TavernTarget tgt{1234};
    TavernSlotEntry e{1234, 5};  // entryId == matchId, ownerWord != 0xFFFF
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, 0);
}

// ---- Match at a later index after non-matching ids --------------------------

TEST(CharacterReconTavern, MatchThird) {
    TavernTarget tgt{1234};
    TavernSlotEntry e0{1, 5};
    TavernSlotEntry e1{2, 5};
    TavernSlotEntry e2{1234, 9};
    auto slots = MakeSlots();
    slots[0].entry = &e0;
    slots[1].entry = &e1;
    slots[2].entry = &e2;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, 2);
}

// ---- Matching id but unassigned ownerWord (0xFFFF) is skipped ---------------

TEST(CharacterReconTavern, SkipUnassignedOwner) {
    TavernTarget tgt{1234};
    TavernSlotEntry e0{1234, 0xFFFF};  // id matches but owner unassigned -> skip
    TavernSlotEntry e1{1234, 3};       // id matches and assigned -> take
    auto slots = MakeSlots();
    slots[0].entry = &e0;
    slots[1].entry = &e1;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, 1);
}

// ---- Null terminator before any match => false ------------------------------

TEST(CharacterReconTavern, NullTerminatesScan) {
    TavernTarget tgt{1234};
    TavernSlotEntry e0{1, 5};
    TavernSlotEntry e2{1234, 9};  // would match, but unreachable past the null
    auto slots = MakeSlots();
    slots[0].entry = &e0;
    slots[1].entry = nullptr;     // terminates the scan at index 1
    slots[2].entry = &e2;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

// ---- Full 16-slot table with no match => false (loop cap edx >= 16) ---------

TEST(CharacterReconTavern, FullTableNoMatch) {
    TavernTarget tgt{1234};
    std::vector<TavernSlotEntry> entries(16);
    auto slots = MakeSlots();
    for (int i = 0; i < 16; ++i) {
        entries[i].entryId = 7000 + i;  // none equal 1234
        entries[i].ownerWord = 1;
        slots[i].entry = &entries[i];
    }
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, -999);
}

// ---- Match in the last (16th) slot of a full table => index 15 --------------

TEST(CharacterReconTavern, MatchLastSlot) {
    TavernTarget tgt{1234};
    std::vector<TavernSlotEntry> entries(16);
    auto slots = MakeSlots();
    for (int i = 0; i < 16; ++i) {
        entries[i].entryId = (i == 15) ? 1234 : (7000 + i);
        entries[i].ownerWord = 1;
        slots[i].entry = &entries[i];
    }
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out, &TurnYes));
    CHECK_EQ(out, 15);
}

// ---- Default predicate keeps the gate open (non-networked case) -------------

TEST(CharacterReconTavern, DefaultPredicateOpen) {
    TavernTarget tgt{1234};
    TavernSlotEntry e{1234, 5};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    // 2-arg overload uses the global predicate; default is open.
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out));
    CHECK_EQ(out, 0);
}

// ---- SetTavernTurnPredicate wires a custom global predicate -----------------

TEST(CharacterReconTavern, SetGlobalPredicate) {
    TavernTarget tgt{1234};
    TavernSlotEntry e{1234, 5};
    auto slots = MakeSlots();
    slots[0].entry = &e;
    TavernObject o = MakeObj(&tgt);
    int out = -999;
    SetTavernTurnPredicate(&TurnNo);
    CHECK(!FindTavernTargetSlot(&o, slots.data(), &out));
    SetTavernTurnPredicate(&TurnYes);
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out));
    CHECK_EQ(out, 0);
    SetTavernTurnPredicate(nullptr);  // restore default (open)
    CHECK(FindTavernTargetSlot(&o, slots.data(), &out));
}
