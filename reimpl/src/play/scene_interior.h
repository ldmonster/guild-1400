#pragma once
// gilde.exe — Building-interior ENTRY + room selection + open-for-active-char +
// combat object-mesh attach (namespace guild::play). MODULE: scene/interior.
//
// Reconstructed 1:1 from the binary:
//   VIBE_Scene_EnterBuildingInterior     0x5066b8  (interior switch + char show)
//   VIBE_Building_SelectRoomToEnter      0x51db4c  (room pick + transition)
//   VIBE_Dialog_OpenBuildingForActiveChar 0x4adef4 (enter-eligibility + dispatch)
//   VIBE_Combat_AttachObjectMesh         0x485e88  (weapon/object mesh attach)
//
// These four are the "step into a building / room" gate the frame loop reaches
// (root 0x4c09a0). They are dense with engine-global side effects (the active
// building dword_631744, the busy flag dword_11BC27C, the scene-graph traversal
// roots, the GameObject query iterator, the character table). Per the project
// pattern we extract the DETERMINISTIC DECISION CORES as pure, golden-pinnable
// functions and route every cross-module side effect through SceneInteriorHooks
// (inert defaults => testable in isolation, real backends installed by the game).
//
// Building-type categories: a building's "category" is a byte read from the
// building-type table dword_13CE294 at stride 589 indexed by the building's type
// byte (building[0]). The two predicates below mirror gilde.exe exactly.
#include "guild/common/types.h"

#include <string>
#include <vector>

namespace guild::play {

// ===========================================================================
// Building-type table predicates (the byte at dword_13CE294 + 589*type).
// ===========================================================================

// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory.
// Maps the building-type CATEGORY byte (table[589*type]) to a coarse class id.
// Pure switch; default 0. (Used by EnterBuildingInterior for season decor logic.)
u8 Building_MapTypeToCategory(u8 categoryByte);

// gilde.exe 0x587f80 — VIBE_Building_IsProductionType.
// True iff the category byte is one of {11,12,13,16,28} (Werkstatt/Hof/Markt/...).
bool Building_IsProductionType(u8 categoryByte);

// ===========================================================================
// Room selection  (gilde.exe 0x51db4c — VIBE_Building_SelectRoomToEnter)
// ===========================================================================
// The original: if (roomIdOrMinusOne == -1) the active building's room field
// (building+0x29) is set directly to the i16 argument; otherwise it QUERY-FINDS
// the building's room objects (QueryFind(building+0x5D,1,4,2) -> first, count in
// ecx) and walks the iterator to pick a room, writing that room's id word into
// building+0x29. Then in BOTH cases it runs the room transition (camera reset +
// "RaumWechseln_s" voice). We split the deterministic PICK (which room id) from
// the transition side effects.

// The room-pick result: which room id word is written to building+0x29, and
// whether a transition should run (false only if the query found nothing).
struct RoomPick {
    bool   found = false;     // a room object was resolved (or direct-id path)
    i16    roomId = 0;        // the id word written to building+0x29
};

// gilde.exe 0x51db4c (pick portion) — pure.
//   roomCount/roomIds model the QueryFind+IterNext walk: roomIds[0] is the first
//   match, and the original advances the iterator until edx (1-based) reaches
//   count, then writes that entry's id. With `roomCount<=1` it uses roomIds[0];
//   with 1<roomCount<=roomIds.size() it uses roomIds[roomCount-1]. If roomCount
//   exceeds roomIds.size() the original's IterNext returns null mid-walk and the
//   function returns NOT FOUND (no write) — it does NOT clamp to the last match.
//   If `directId` is provided (caller passed -1 to the original) the pick
//   is that id with found=true and no query is performed.
RoomPick SelectRoom_Pick(const std::vector<i16>& roomIds, int roomCount);
RoomPick SelectRoom_Direct(i16 directId);

// ===========================================================================
// Enter-eligibility gate  (gilde.exe 0x4adef4 portion)
// ===========================================================================
// Before opening a building for the active character the original checks:
//   * busy flag dword_11BC27C must be 0 (else abort, do nothing);
//   * if there IS an active char (word_63CC5C != -1) and its table row is live,
//     the building must be enterable: its category is 6 (always OK), OR category
//     4 AND the building's owner field (building+0x65) equals the active char's
//     owned-building id (dword_12CE914[134*char]); any other category aborts.
//   * if word_63CC5C == -1 or the char row is empty, eligibility passes.
// We model the gate as a pure predicate over the resolved inputs.
struct EnterGateInputs {
    bool busy = false;            // dword_11BC27C != 0
    bool hasActiveChar = false;   // word_63CC5C != -1
    bool activeCharLive = false;  // byte_12CEAC1[536*char] != 0
    u8   buildingCategory = 0;    // table[589*building[0]]
    bool ownerMatches = false;    // building+0x65 == dword_12CE914[134*char]
};
// Returns true iff the open should proceed (eligibility passes AND not busy).
bool OpenBuilding_GateAllows(const EnterGateInputs& in);

// ===========================================================================
// Combat object-mesh attach  (gilde.exe 0x485e88 — VIBE_Combat_AttachObjectMesh)
// ===========================================================================
// Given a unit, the original finds its attached object-def (FindObjectDef). If a
// def with a nonzero id exists AND the unit has a character actor (unit+0x184):
//   name = "ob_" + UPPER(defName);
//   if the actor's currently-attached slot-2 item name (actor+0x64*... actor[25]
//   here as +0x64) is empty OR differs from `name` (case-insensitive):
//     if the actor already has a slot-2 item, detach it (AttachItemToBone(.,2,0));
//     attach the new one (AttachItemToBone(.,2,name)).
// If there is NO def (or zero id) but the actor exists and has a slot-2 item, the
// item is detached (AttachItemToBone(.,2,0)).
// We split the NAME-BUILD + the equal/differ DECISION (pure) from the attach
// side effects (hooks).
enum class AttachMeshAction {
    kNone,        // no actor, or nothing to do
    kDetachOnly,  // detach the existing slot-2 item (def absent / no def id)
    kReplace,     // detach-if-present then attach the new "ob_<NAME>" item
    kKeep,        // current item already equals the desired name -> no change
};
struct AttachMeshDecision {
    AttachMeshAction action = AttachMeshAction::kNone;
    std::string      itemName;   // the "ob_<UPPERNAME>" to attach (kReplace only)
    bool             hadExisting = false; // actor had a slot-2 item before
};
// `defName` is the resolved object-def name (empty/none => def absent path).
// `hasDef` distinguishes "no def / zero id" from "def with empty name".
// `hasActor` is unit+0x184 != 0; `existingItemName` is the actor's current slot-2
// item name (empty if none).
AttachMeshDecision AttachObjectMesh_Decide(bool hasActor, bool hasDef,
                                           const std::string& defName,
                                           bool hasExistingItem,
                                           const std::string& existingItemName);

// ===========================================================================
// Building-interior ENTER decision core (gilde.exe 0x5066b8)
// ===========================================================================
// VIBE_Scene_EnterBuildingInterior switches the visible scene to a building's
// interior. The orchestration (scene-graph traversals, storage-room attach,
// character collection) is engine-global; the genuinely deterministic decisions
// are extracted here:
//   * the season clamp: byte_634484 := season only when season in 0..3;
//   * the production-decor decision: production-type buildings hide foliage decor
//     + the upgrade scaffold (TraverseTree(HideFoliageDecor)/(HideUpgradeScaffold));
//   * the plant-trigger: building type 30 entered with the 253 "default room"
//     filter loads its vegetation models (EnsureModelsLoaded(building+113));
//   * the interior char-show cap: at most 4 characters are shown when the
//     interior's mode byte (*v4) == 1, else at most 8.

struct EnterInteriorDecision {
    bool   updateSeason = false;   // season was in 0..3 -> write byte_634484
    u8     seasonToWrite = 0;
    bool   hideProductionDecor = false; // IsProductionType -> hide foliage+scaffold
    bool   loadPlantModels = false;     // type 30 + room filter 253
    int    maxVisibleChars = 8;         // 4 if interiorModeByte==1 else 8
};

// gilde.exe 0x5066b8 (decision portion) — pure.
//   `season`           = VIBE_GameTime_GetSeasonFromDay(...) result (u8).
//   `buildingType`     = building[0].
//   `categoryByte`     = table[589*buildingType] (for IsProductionType).
//   `roomFilter`       = the a2 (dx) high-word room filter (HIWORD(v23)).
//   `interiorModeByte` = *v4 where v4 = dword_13CE294 + 589*buildingType (==
//                        categoryByte's table row first byte; here passed in).
EnterInteriorDecision EnterBuildingInterior_Decide(u8 season, u8 buildingType,
                                                   u8 categoryByte, i16 roomFilter,
                                                   u8 interiorModeByte);

// ===========================================================================
// Orchestration hooks (cross-module side effects).
// ===========================================================================
struct SceneInteriorHooks {
    // --- room transition (SelectRoomToEnter tail, gilde.exe 0x51db5d) ----------
    // Reset the camera-step counters: dword_631614 = dword_631610-dword_631618+1.
    void (*resetRoomCameraStep)() = nullptr;
    // VIBE_Object_SetPosition(cameraObj, &pos)/SetWorldTranslation — the room cam.
    void (*resetRoomCameraPose)() = nullptr;
    // Clear dword_6316CC / dword_6316D0 (room scroll accumulators).
    void (*clearRoomScroll)() = nullptr;
    // VIBE_Audio_StartVoiceSample(bank,63,1,vol,127,0,0) — "RaumWechseln_s".
    void (*playRoomChangeVoice)(int volume) = nullptr;
    // Write the picked room id into building+0x29.
    void (*writeBuildingRoom)(i16 roomId) = nullptr;

    // --- AttachObjectMesh (gilde.exe 0x485e88) ---------------------------------
    // VIBE_Character_AttachItemToBone(actor, 2, name-or-null).
    void (*attachItemToBone)(void* actor, const char* name) = nullptr;
};
void SetSceneInteriorHooks(const SceneInteriorHooks* hooks);
const SceneInteriorHooks& GetSceneInteriorHooks();
void ResetSceneInteriorHooks();

// ===========================================================================
// Wired orchestrators (apply the pure decision, then fire the hooks).
// ===========================================================================

// gilde.exe 0x51db4c — full. `directId` non-null => the building+0x29 := *directId
// path (original `a1 == -1`); else uses the query pick. Returns the picked room.
RoomPick Building_SelectRoomToEnter(const std::vector<i16>& roomIds, int roomCount,
                                    const i16* directId, int voiceVolume);

// gilde.exe 0x485e88 — full. Applies the decision via attachItemToBone. `actor`
// is the opaque actor pointer the hook receives. Returns the decision taken.
AttachMeshDecision Combat_AttachObjectMesh(void* actor, bool hasDef,
                                           const std::string& defName,
                                           bool hasExistingItem,
                                           const std::string& existingItemName);

}  // namespace guild::play
