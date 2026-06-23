#include "play/scene_interior.h"

#include "sim/building_type.h"  // guild::sim::Building_MapKindToCategory (0x5878b0)
                                // + Building_IsProductionKind (0x587f80)

#include <cctype>
#include <cstring>

namespace guild::play {

// ===========================================================================
// Hooks
// ===========================================================================
static const SceneInteriorHooks g_inert{};
static const SceneInteriorHooks* g_hooks = &g_inert;
void SetSceneInteriorHooks(const SceneInteriorHooks* hooks) { g_hooks = hooks ? hooks : &g_inert; }
const SceneInteriorHooks& GetSceneInteriorHooks() { return *g_hooks; }
void ResetSceneInteriorHooks() { g_hooks = &g_inert; }

// ===========================================================================
// Building-type table predicates
// ===========================================================================

// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory.
// REUSE the canonical reconstruction in guild::sim (building_lifecycle.cpp); do
// not redefine the body (ODR). This is a thin play-side accessor over the same
// switch the original applies to the type-record kind byte (table[589*type]).
u8 Building_MapTypeToCategory(u8 categoryByte) {
    return guild::sim::Building_MapKindToCategory(categoryByte);
}

// gilde.exe 0x587f80 — VIBE_Building_IsProductionType.
// REUSE guild::sim::Building_IsProductionKind (building_type.cpp): kind in
// {11,12,13,16,28}.
bool Building_IsProductionType(u8 categoryByte) {
    return guild::sim::Building_IsProductionKind(categoryByte);
}

// ===========================================================================
// Room selection pick (gilde.exe 0x51db4c)
// ===========================================================================
RoomPick SelectRoom_Direct(i16 directId) {
    RoomPick p;
    p.found = true;
    p.roomId = directId;
    return p;
}

RoomPick SelectRoom_Pick(const std::vector<i16>& roomIds, int roomCount) {
    RoomPick p;
    if (roomIds.empty() || roomCount <= 0) {
        // QueryFind returned null -> the original returns without writing/transition.
        p.found = false;
        return p;
    }
    // gilde.exe 0x51db4c disasm (mov ecx,eax => ecx is the count `a1`):
    //   edx=1; cmp ecx,1; jle LABEL_9 (count<=1 -> write roomIds[0]).
    //   loop: result=IterNext(); if !result -> return null (loc_51DBB9, no write);
    //         inc edx; cmp edx,ecx; jl loop.   Exit when edx>=count -> LABEL_9
    //         writes the CURRENT result = the (count-1)-th IterNext = roomIds[count-1].
    // Modeled with roomIds[0]=QueryFind result, roomIds[k]=k-th IterNext:
    //   count<=1            -> roomIds[0].
    //   1<count<=n          -> roomIds[count-1].
    //   count>n             -> IterNext walks off the end and returns null at
    //                          index n while edx<count -> the original returns
    //                          NULL (not found, no write). NOT a clamp to the last.
    int n = static_cast<int>(roomIds.size());
    int idx;
    if (roomCount <= 1) {
        idx = 0;
    } else if (roomCount <= n) {
        idx = roomCount - 1;
    } else {
        // count exceeds available matches: IterNext returns null -> not found.
        p.found = false;
        return p;
    }
    p.found = true;
    p.roomId = roomIds[static_cast<size_t>(idx)];
    return p;
}

// ===========================================================================
// Enter-eligibility gate (gilde.exe 0x4adef4 head)
// ===========================================================================
bool OpenBuilding_GateAllows(const EnterGateInputs& in) {
    if (in.busy)
        return false;  // dword_11BC27C != 0 -> the whole body is skipped.

    // If no active char, or the char row is empty, eligibility passes (the
    // original jumps straight to the load path at loc_4ADF9B).
    if (!in.hasActiveChar || !in.activeCharLive)
        return true;

    // category 6 -> OK. category 4 -> OK only if owner matches. else -> abort.
    if (in.buildingCategory == 6)
        return true;
    if (in.buildingCategory == 4)
        return in.ownerMatches;
    return false;
}

// ===========================================================================
// Combat object-mesh attach decision (gilde.exe 0x485e88)
// ===========================================================================
static bool EqualNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

AttachMeshDecision AttachObjectMesh_Decide(bool hasActor, bool hasDef,
                                           const std::string& defName,
                                           bool hasExistingItem,
                                           const std::string& existingItemName) {
    AttachMeshDecision d;
    d.hadExisting = hasExistingItem;

    if (!hasActor) {
        // No actor (unit+0x184 == 0) -> nothing happens in either branch.
        d.action = AttachMeshAction::kNone;
        return d;
    }

    if (!hasDef) {
        // FindObjectDef null or zero id -> loc_485F47: detach slot-2 if present.
        d.action = hasExistingItem ? AttachMeshAction::kDetachOnly
                                   : AttachMeshAction::kNone;
        return d;
    }

    // def with id present -> build "ob_<UPPER(name)>".
    std::string upper = defName;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    d.itemName = "ob_" + upper;

    // if current slot-2 item empty OR differs (case-insensitive) -> replace.
    if (!hasExistingItem || !EqualNoCase(existingItemName, d.itemName)) {
        d.action = AttachMeshAction::kReplace;
    } else {
        d.action = AttachMeshAction::kKeep;
    }
    return d;
}

// ===========================================================================
// Building-interior ENTER decision core (gilde.exe 0x5066b8)
// ===========================================================================
EnterInteriorDecision EnterBuildingInterior_Decide(u8 season, u8 buildingType,
                                                   u8 categoryByte, i16 roomFilter,
                                                   u8 interiorModeByte) {
    EnterInteriorDecision d;

    // if (season <= 2 || season == 3) byte_634484 = season;  (i.e. season <= 3)
    if (season <= 3) {
        d.updateSeason = true;
        d.seasonToWrite = season;
    }

    // if (IsProductionType(building)) hide foliage decor + upgrade scaffold.
    d.hideProductionDecor = Building_IsProductionType(categoryByte);

    // if (building[0] == 30 && roomFilter == 253) EnsureModelsLoaded(plot).
    d.loadPlantModels = (buildingType == 30) &&
                        (static_cast<u16>(roomFilter) == 253);

    // v13 = (*v4 == 1) ? 4 : 8;  where v4 = 589*building[0] + dword_13CE294, so
    // *v4 is the building-type table's kind byte = the SAME byte IsProductionType
    // reads (table[589*type]). Caller passes it as interiorModeByte.
    d.maxVisibleChars = (interiorModeByte == 1) ? 4 : 8;
    return d;
}

// ===========================================================================
// Wired orchestrators
// ===========================================================================
RoomPick Building_SelectRoomToEnter(const std::vector<i16>& roomIds, int roomCount,
                                    const i16* directId, int voiceVolume) {
    const SceneInteriorHooks& h = *g_hooks;
    RoomPick pick = directId ? SelectRoom_Direct(*directId)
                             : SelectRoom_Pick(roomIds, roomCount);
    if (!pick.found)
        return pick;  // original returns the null query result, no transition.

    // building+0x29 = roomId
    if (h.writeBuildingRoom)
        h.writeBuildingRoom(pick.roomId);
    // transition: camera-step reset, camera pose reset, clear room scroll, voice.
    if (h.resetRoomCameraStep) h.resetRoomCameraStep();
    if (h.resetRoomCameraPose) h.resetRoomCameraPose();
    if (h.clearRoomScroll) h.clearRoomScroll();
    if (h.playRoomChangeVoice) h.playRoomChangeVoice(voiceVolume);
    return pick;
}

AttachMeshDecision Combat_AttachObjectMesh(void* actor, bool hasDef,
                                           const std::string& defName,
                                           bool hasExistingItem,
                                           const std::string& existingItemName) {
    const SceneInteriorHooks& h = *g_hooks;
    AttachMeshDecision d =
        AttachObjectMesh_Decide(actor != nullptr, hasDef, defName,
                                hasExistingItem, existingItemName);
    switch (d.action) {
        case AttachMeshAction::kDetachOnly:
            if (h.attachItemToBone) h.attachItemToBone(actor, nullptr);
            break;
        case AttachMeshAction::kReplace:
            // original: if existing present, detach first, then attach new.
            if (d.hadExisting && h.attachItemToBone)
                h.attachItemToBone(actor, nullptr);
            if (h.attachItemToBone)
                h.attachItemToBone(actor, d.itemName.c_str());
            break;
        case AttachMeshAction::kKeep:
        case AttachMeshAction::kNone:
            break;
    }
    return d;
}

}  // namespace guild::play
