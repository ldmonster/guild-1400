// scene_interior_test.cpp — golden-vector unit tests for play/scene_interior:
// building-type predicates, room selection, enter-eligibility gate, the building-
// interior enter decision core, and the combat object-mesh attach decision.
// All pins are taken directly from the gilde.exe decompile. Suite: SCI.
#include "test.h"

#include "play/scene_interior.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

// ---------------------------------------------------------------------------
// Building_MapTypeToCategory — full 1:1 case table (gilde.exe 0x5878b0).
// ---------------------------------------------------------------------------
TEST(SCI, MapTypeToCategory) {
    // category 3 group {1,3,6,0xF}
    CHECK_EQ(Building_MapTypeToCategory(1), (u8)3);
    CHECK_EQ(Building_MapTypeToCategory(3), (u8)3);
    CHECK_EQ(Building_MapTypeToCategory(6), (u8)3);
    CHECK_EQ(Building_MapTypeToCategory(0xF), (u8)3);
    // 2 -> 6
    CHECK_EQ(Building_MapTypeToCategory(2), (u8)6);
    // {4,5,9} -> 8
    CHECK_EQ(Building_MapTypeToCategory(4), (u8)8);
    CHECK_EQ(Building_MapTypeToCategory(5), (u8)8);
    CHECK_EQ(Building_MapTypeToCategory(9), (u8)8);
    // 7 -> 4
    CHECK_EQ(Building_MapTypeToCategory(7), (u8)4);
    // {8,0xE,0x12,0x14,0x15,0x16} -> 1
    CHECK_EQ(Building_MapTypeToCategory(8), (u8)1);
    CHECK_EQ(Building_MapTypeToCategory(0xE), (u8)1);
    CHECK_EQ(Building_MapTypeToCategory(0x12), (u8)1);
    CHECK_EQ(Building_MapTypeToCategory(0x16), (u8)1);
    // {0xB,0xC,0xD} -> 2
    CHECK_EQ(Building_MapTypeToCategory(0xB), (u8)2);
    CHECK_EQ(Building_MapTypeToCategory(0xD), (u8)2);
    // 0x13 -> 7
    CHECK_EQ(Building_MapTypeToCategory(0x13), (u8)7);
    // {0x17..0x1A} -> 5
    CHECK_EQ(Building_MapTypeToCategory(0x17), (u8)5);
    CHECK_EQ(Building_MapTypeToCategory(0x1A), (u8)5);
    // default 0
    CHECK_EQ(Building_MapTypeToCategory(0), (u8)0);
    CHECK_EQ(Building_MapTypeToCategory(0xFF), (u8)0);
    CHECK_EQ(Building_MapTypeToCategory(10), (u8)0);  // 0xA not mapped
}

// ---------------------------------------------------------------------------
// Building_IsProductionType — set {11,12,13,16,28} (gilde.exe 0x587f80).
// ---------------------------------------------------------------------------
TEST(SCI, IsProductionType) {
    for (int c = 0; c < 256; ++c) {
        bool expect = (c == 11 || c == 12 || c == 13 || c == 16 || c == 28);
        CHECK_EQ(Building_IsProductionType((u8)c), expect);
    }
}

// ---------------------------------------------------------------------------
// Room selection pick (gilde.exe 0x51db4c).
// ---------------------------------------------------------------------------
TEST(SCI, RoomPick_Direct) {
    RoomPick p = SelectRoom_Direct((i16)42);
    CHECK(p.found);
    CHECK_EQ(p.roomId, (i16)42);
}

TEST(SCI, RoomPick_Empty) {
    RoomPick p = SelectRoom_Pick({}, 0);
    CHECK(!p.found);
    // count > 0 but no ids also yields not-found (QueryFind null).
    RoomPick p2 = SelectRoom_Pick({}, 3);
    CHECK(!p2.found);
}

TEST(SCI, RoomPick_SingleOrCountOne) {
    // count <= 1 -> first match (roomIds[0]).
    RoomPick p = SelectRoom_Pick({7, 8, 9}, 1);
    CHECK(p.found);
    CHECK_EQ(p.roomId, (i16)7);
}

TEST(SCI, RoomPick_WalksToCountth) {
    // count = 3 -> the iterator is walked to the 3rd match -> roomIds[2].
    RoomPick p = SelectRoom_Pick({10, 20, 30, 40}, 3);
    CHECK(p.found);
    CHECK_EQ(p.roomId, (i16)30);
    // count exceeding available -> the original (0x51db4c disasm: loop calls
    // IterNext while edx<count) walks past the last match, IterNext returns null,
    // and the function returns NOT FOUND (no write). It does NOT clamp.
    RoomPick p2 = SelectRoom_Pick({10, 20}, 5);
    CHECK(!p2.found);
    // count == available -> the last match (edx reaches count exactly).
    RoomPick p3 = SelectRoom_Pick({10, 20}, 2);
    CHECK(p3.found);
    CHECK_EQ(p3.roomId, (i16)20);
}

// Wired orchestrator fires transition hooks only when a room is found.
namespace {
struct RoomCalls {
    int writeRoom = -1;
    int camStep = 0, camPose = 0, clearScroll = 0, voiceVol = -999;
};
RoomCalls g_rc;
void w_writeRoom(i16 id) { g_rc.writeRoom = id; }
void w_camStep() { g_rc.camStep++; }
void w_camPose() { g_rc.camPose++; }
void w_clearScroll() { g_rc.clearScroll++; }
void w_voice(int v) { g_rc.voiceVol = v; }
}  // namespace

TEST(SCI, SelectRoomToEnter_FiresTransition) {
    g_rc = RoomCalls{};
    SceneInteriorHooks h{};
    h.writeBuildingRoom = w_writeRoom;
    h.resetRoomCameraStep = w_camStep;
    h.resetRoomCameraPose = w_camPose;
    h.clearRoomScroll = w_clearScroll;
    h.playRoomChangeVoice = w_voice;
    SetSceneInteriorHooks(&h);

    i16 direct = 55;
    RoomPick p = Building_SelectRoomToEnter({}, 0, &direct, 127);
    CHECK(p.found);
    CHECK_EQ(g_rc.writeRoom, 55);
    CHECK_EQ(g_rc.camStep, 1);
    CHECK_EQ(g_rc.camPose, 1);
    CHECK_EQ(g_rc.clearScroll, 1);
    CHECK_EQ(g_rc.voiceVol, 127);

    // Not-found -> no transition.
    g_rc = RoomCalls{};
    RoomPick p2 = Building_SelectRoomToEnter({}, 0, nullptr, 127);
    CHECK(!p2.found);
    CHECK_EQ(g_rc.writeRoom, -1);
    CHECK_EQ(g_rc.camStep, 0);
    ResetSceneInteriorHooks();
}

// ---------------------------------------------------------------------------
// Enter-eligibility gate (gilde.exe 0x4adef4 head).
// ---------------------------------------------------------------------------
TEST(SCI, OpenGate_Busy) {
    EnterGateInputs in;
    in.busy = true;
    CHECK(!OpenBuilding_GateAllows(in));
}

TEST(SCI, OpenGate_NoActiveChar) {
    EnterGateInputs in;
    in.hasActiveChar = false;
    CHECK(OpenBuilding_GateAllows(in));   // jumps straight to load path
}

TEST(SCI, OpenGate_DeadCharRow) {
    EnterGateInputs in;
    in.hasActiveChar = true;
    in.activeCharLive = false;
    CHECK(OpenBuilding_GateAllows(in));
}

TEST(SCI, OpenGate_Category6) {
    EnterGateInputs in;
    in.hasActiveChar = true;
    in.activeCharLive = true;
    in.buildingCategory = 6;
    CHECK(OpenBuilding_GateAllows(in));
}

TEST(SCI, OpenGate_Category4_OwnerGates) {
    EnterGateInputs in;
    in.hasActiveChar = true;
    in.activeCharLive = true;
    in.buildingCategory = 4;
    in.ownerMatches = false;
    CHECK(!OpenBuilding_GateAllows(in));
    in.ownerMatches = true;
    CHECK(OpenBuilding_GateAllows(in));
}

TEST(SCI, OpenGate_OtherCategoryAborts) {
    EnterGateInputs in;
    in.hasActiveChar = true;
    in.activeCharLive = true;
    in.buildingCategory = 5;
    in.ownerMatches = true;  // irrelevant for non-4/6
    CHECK(!OpenBuilding_GateAllows(in));
}

// ---------------------------------------------------------------------------
// EnterBuildingInterior decision core (gilde.exe 0x5066b8).
// ---------------------------------------------------------------------------
TEST(SCI, EnterInterior_SeasonClamp) {
    for (int s = 0; s <= 3; ++s) {
        auto d = EnterBuildingInterior_Decide((u8)s, 1, 0, 0, 0);
        CHECK(d.updateSeason);
        CHECK_EQ(d.seasonToWrite, (u8)s);
    }
    auto d4 = EnterBuildingInterior_Decide(4, 1, 0, 0, 0);
    CHECK(!d4.updateSeason);
    auto d255 = EnterBuildingInterior_Decide(255, 1, 0, 0, 0);
    CHECK(!d255.updateSeason);
}

TEST(SCI, EnterInterior_ProductionDecor) {
    // category 11 is production -> hide decor.
    auto d = EnterBuildingInterior_Decide(0, 5, 11, 0, 0);
    CHECK(d.hideProductionDecor);
    // non-production category -> no.
    auto d2 = EnterBuildingInterior_Decide(0, 5, 6, 0, 0);
    CHECK(!d2.hideProductionDecor);
}

TEST(SCI, EnterInterior_PlantTrigger) {
    // type 30 + room filter 253 -> load plant models.
    auto d = EnterBuildingInterior_Decide(0, 30, 0, (i16)253, 0);
    CHECK(d.loadPlantModels);
    // wrong type.
    CHECK(!EnterBuildingInterior_Decide(0, 29, 0, (i16)253, 0).loadPlantModels);
    // wrong room filter.
    CHECK(!EnterBuildingInterior_Decide(0, 30, 0, (i16)1, 0).loadPlantModels);
}

TEST(SCI, EnterInterior_CharCap) {
    // interior mode byte == 1 -> 4, else 8.
    CHECK_EQ(EnterBuildingInterior_Decide(0, 1, 0, 0, 1).maxVisibleChars, 4);
    CHECK_EQ(EnterBuildingInterior_Decide(0, 1, 0, 0, 0).maxVisibleChars, 8);
    CHECK_EQ(EnterBuildingInterior_Decide(0, 1, 0, 0, 2).maxVisibleChars, 8);
}

// ---------------------------------------------------------------------------
// Combat object-mesh attach decision (gilde.exe 0x485e88).
// ---------------------------------------------------------------------------
TEST(SCI, AttachMesh_NoActor) {
    auto d = AttachObjectMesh_Decide(/*hasActor*/false, /*hasDef*/true, "sword",
                                     /*hasExisting*/true, "ob_OLD");
    CHECK(d.action == AttachMeshAction::kNone);
}

TEST(SCI, AttachMesh_NoDef_DetachExisting) {
    auto d = AttachObjectMesh_Decide(true, /*hasDef*/false, "", true, "ob_OLD");
    CHECK(d.action == AttachMeshAction::kDetachOnly);
    auto d2 = AttachObjectMesh_Decide(true, false, "", /*hasExisting*/false, "");
    CHECK(d2.action == AttachMeshAction::kNone);
}

TEST(SCI, AttachMesh_Replace_BuildsUpperName) {
    auto d = AttachObjectMesh_Decide(true, true, "sword", false, "");
    CHECK(d.action == AttachMeshAction::kReplace);
    CHECK(d.itemName == "ob_SWORD");
}

TEST(SCI, AttachMesh_Keep_WhenSameNoCase) {
    // existing equals desired (case-insensitive) -> keep.
    auto d = AttachObjectMesh_Decide(true, true, "Axe", true, "OB_axe");
    CHECK(d.action == AttachMeshAction::kKeep);
    CHECK(d.itemName == "ob_AXE");
}

TEST(SCI, AttachMesh_Replace_WhenDiffers) {
    auto d = AttachObjectMesh_Decide(true, true, "Axe", true, "ob_SWORD");
    CHECK(d.action == AttachMeshAction::kReplace);
    CHECK(d.hadExisting);
}

// Wired orchestrator: replace with an existing item issues detach THEN attach.
namespace {
struct AttachCalls { std::vector<std::string> calls; };
AttachCalls g_ac;
void w_attach(void* /*actor*/, const char* name) {
    g_ac.calls.push_back(name ? std::string(name) : std::string("<null>"));
}
}  // namespace

TEST(SCI, AttachMesh_Wired_ReplaceSequence) {
    g_ac = AttachCalls{};
    SceneInteriorHooks h{};
    h.attachItemToBone = w_attach;
    SetSceneInteriorHooks(&h);

    int dummyActor = 0;
    auto d = Combat_AttachObjectMesh(&dummyActor, true, "sword", true, "ob_OLD");
    CHECK(d.action == AttachMeshAction::kReplace);
    CHECK_EQ((int)g_ac.calls.size(), 2);
    CHECK(g_ac.calls[0] == "<null>");      // detach existing
    CHECK(g_ac.calls[1] == "ob_SWORD");    // attach new

    // detach-only path: one null call.
    g_ac = AttachCalls{};
    Combat_AttachObjectMesh(&dummyActor, false, "", true, "ob_OLD");
    CHECK_EQ((int)g_ac.calls.size(), 1);
    CHECK(g_ac.calls[0] == "<null>");

    // keep path: no calls.
    g_ac = AttachCalls{};
    Combat_AttachObjectMesh(&dummyActor, true, "axe", true, "ob_AXE");
    CHECK_EQ((int)g_ac.calls.size(), 0);

    ResetSceneInteriorHooks();
}
