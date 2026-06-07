#include "sim/command_unit_orders.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Module-modeled state (the originals' file globals) + leaf hooks.
// ===========================================================================
namespace {

UnitOrderLog g_log;

// The modeled live-unit set (word_B5A350 in the binary). A flat vector keyed by
// unitId is enough for the leaves' FindUnitById usage.
std::vector<UnitOrderActor> g_units;

// dword_6311F0 — the "last person record the select-sound named" latch. The
// original stores the resolved record pointer; we keep the resolved person id.
i32 g_lastSelectedPerson = 0;

// --- default leaf backends --------------------------------------------------

UnitOrderActor* DefaultFindUnit(i32 unitId) {
    for (auto& u : g_units)
        if (u.unitId == unitId)
            return &u;
    return nullptr;
}

void DefaultStandUp(i32 actorPtr) {
    ++g_log.standUpCount;
    g_log.lastStandUpActor = actorPtr;
}

int DefaultReleaseCarried(i32 /*carriedHandle*/, i32 /*actorPtr*/) {
    ++g_log.releaseCarriedCount;
    return 1;
}

void DefaultInsertAction(i32 actorPtr, int /*tag*/, const char* /*name*/) {
    ++g_log.pickupActionCount;
    g_log.lastPickupActor = actorPtr;
}

// Default person-name resolver: a person id is "found" iff it is non-negative;
// the modeled name index is the low 16 bits of the id (stable, verifiable).
u16 DefaultFindPersonName(i32 personId, bool& found) {
    found = (personId >= 0);
    return static_cast<u16>(personId & 0xFFFF);
}

void DefaultBanner(int textId, u16 nameIndex) {
    ++g_log.bannerCount;
    g_log.lastBannerTextId = textId;
    g_log.lastBannerPerson = nameIndex;
}

// Synthetic, ever-positive flag-mesh handle source.
i32 g_flagMeshNext = 0x70000;
i32 DefaultFlagAttach(i32 unitId, u8 /*personType*/) {
    ++g_log.flagAttachCount;
    g_log.lastFlagUnit = unitId;
    return g_flagMeshNext++;
}
void DefaultFlagDetach(i32 /*meshHandle*/) {
    ++g_log.flagDetachCount;
}

void DefaultScenePosition(i32 /*sceneObject*/, const float[3]) {
    ++g_log.scenePosUpdates;
}
void DefaultSceneTranslation(i32 /*sceneObject*/, const float[3]) {
    ++g_log.sceneDirUpdates;
}

FindUnitFn       g_findUnit       = &DefaultFindUnit;
StandUpFn        g_standUp        = &DefaultStandUp;
ReleaseCarriedFn g_releaseCarried = &DefaultReleaseCarried;
InsertActionFn   g_insertAction   = &DefaultInsertAction;
FindPersonNameFn g_findPerson     = &DefaultFindPersonName;
BannerFn         g_banner         = &DefaultBanner;
FlagAttachFn     g_flagAttach     = &DefaultFlagAttach;
FlagDetachFn     g_flagDetach     = &DefaultFlagDetach;
SceneSetVecFn    g_scenePos       = &DefaultScenePosition;
SceneSetVecFn    g_sceneTrans     = &DefaultSceneTranslation;

// gilde.exe 0x61bb1c — aSpezialEinhaen_0 ("spezial/einhaendig_vom_boden_nehmen").
constexpr char kPickupActionName[] = "spezial/einhaendig_vom_boden_nehmen";
// The pick-up action's frame tag (HIDWORD(v8) = 46 in InsertActionVararg call).
constexpr int kPickupActionTag = 46;
// VIBE_Text_RenderFormattedMessage format-string ids the two banners use.
constexpr int kBannerSelectSoundTextId = 3610; // ExecUnitSelectSound
constexpr int kBannerConquerFlagTextId = 3611; // ExecConquerFlag

} // namespace

// ===========================================================================
// Hook + state setters.
// ===========================================================================
void SetFindUnitHook(FindUnitFn fn)             { g_findUnit = fn ? fn : &DefaultFindUnit; }
void SetStandUpHook(StandUpFn fn)               { g_standUp = fn ? fn : &DefaultStandUp; }
void SetReleaseCarriedHook(ReleaseCarriedFn fn) { g_releaseCarried = fn ? fn : &DefaultReleaseCarried; }
void SetInsertActionHook(InsertActionFn fn)     { g_insertAction = fn ? fn : &DefaultInsertAction; }
void SetFindPersonNameHook(FindPersonNameFn fn) { g_findPerson = fn ? fn : &DefaultFindPersonName; }
void SetBannerHook(BannerFn fn)                 { g_banner = fn ? fn : &DefaultBanner; }
void SetFlagAttachHook(FlagAttachFn fn)         { g_flagAttach = fn ? fn : &DefaultFlagAttach; }
void SetFlagDetachHook(FlagDetachFn fn)         { g_flagDetach = fn ? fn : &DefaultFlagDetach; }
void SetScenePositionHook(SceneSetVecFn fn)     { g_scenePos = fn ? fn : &DefaultScenePosition; }
void SetSceneTranslationHook(SceneSetVecFn fn)  { g_sceneTrans = fn ? fn : &DefaultSceneTranslation; }

const UnitOrderLog& UnitOrderLogRef() { return g_log; }

UnitOrderActor* SeedUnit(const UnitOrderActor& u) {
    g_units.push_back(u);
    return &g_units.back();
}

void ResetUnitOrderState() {
    g_log = UnitOrderLog{};
    g_units.clear();
    g_lastSelectedPerson = 0;
    g_flagMeshNext = 0x70000;
    g_findUnit = &DefaultFindUnit;
    g_standUp = &DefaultStandUp;
    g_releaseCarried = &DefaultReleaseCarried;
    g_insertAction = &DefaultInsertAction;
    g_findPerson = &DefaultFindPersonName;
    g_banner = &DefaultBanner;
    g_flagAttach = &DefaultFlagAttach;
    g_flagDetach = &DefaultFlagDetach;
    g_scenePos = &DefaultScenePosition;
    g_sceneTrans = &DefaultSceneTranslation;
}

// VIBE_Math_VectorWithinTolerance @0x5caa4c — reused from character_social.cpp
// (declared in character_social.h, included via the header). One definition.

// ===========================================================================
// gilde.exe 0x491054 — VIBE_Command_ExecPickupGroundItem.
// ===========================================================================
bool ExecPickupGroundItem(const UnitOrderRecord& rec) {
    // if ( *(_BYTE *)(a1 + 4) && *(_DWORD *)a1 != -1 )
    if (!rec.active || rec.unitId == -1)
        return false;

    // UnitById = VIBE_Combat_FindUnitById(*(_DWORD *)a1);
    UnitOrderActor* unit = g_findUnit(rec.unitId);
    if (!unit)
        return false; // the original dereferences unconditionally; we guard.

    // VIBE_Character_StandUp(*((_DWORD *)UnitById + 97));  // actorPtr
    g_standUp(unit->actorPtr);

    // VIBE_Object_FindByHandle(...) — locates the carried-object record; its only
    // observable effect here is the release path below, gated on unit[107].
    // if ( *((_DWORD *)UnitById + 107) ) { ApplyParentTransform; ToggleSuspend;
    //                                      *((_DWORD *)UnitById + 107) = 0; }
    if (unit->carriedObject) {
        g_releaseCarried(unit->carriedObject, unit->actorPtr);
        unit->carriedObject = 0;
    }

    // v6 = VIBE_CharAction_InsertActionVararg(<unit[97], tag 46>) + 240;  then the
    // do/while copies the action-name string into v6 (the action's name field).
    g_insertAction(unit->actorPtr, kPickupActionTag, kPickupActionName);
    return true;
}

// ===========================================================================
// gilde.exe 0x49110C — VIBE_Command_ExecUnitSelectSound.
// ===========================================================================
bool ExecUnitSelectSound(const UnitOrderRecord& rec) {
    // dword_6311F0 = (int)VIBE_Person_FindRecordById(*a1);
    bool found = false;
    u16 nameIndex = g_findPerson(rec.unitId, found);
    g_lastSelectedPerson = rec.unitId;
    if (!found)
        return false; // original deref of a null record would crash; we guard.

    // VIBE_Text_RenderFormattedMessage(v4, 3610, *(unsigned __int16 *)dword_6311F0);
    // return VIBE_Hud_SetStatusBannerText(v4);
    g_banner(kBannerSelectSoundTextId, nameIndex);
    return true;
}

// ===========================================================================
// gilde.exe 0x491148 — VIBE_Command_ExecConquerFlag.
// ===========================================================================
bool ExecConquerFlag(const UnitOrderRecord& rec) {
    // v3 = 0;  if ( *(_BYTE *)(a1 + 4) ) { ... }   (a2 = the flag-anchor record)
    if (!rec.active)
        return false;

    // VIBE_Object_FindByHandle(...) resolves the flag-anchor scene record (v4); we
    // model it as the unit that owns this conquer flag. The anchor unit is the one
    // the order points at; here we reuse rec.unitId as both flag holder + anchor.
    UnitOrderActor* anchor = (rec.unitId != -1) ? g_findUnit(rec.unitId) : nullptr;

    UnitOrderActor* newHolder = nullptr;
    if (rec.unitId != -1) {
        // UnitById = FindUnitById(*v2);  *(byte*)(unit[97]+141) |= 4;  v3 = UnitById;
        // banner 3611 with the unit's name word (*UnitById).
        newHolder = anchor;
        if (newHolder) {
            // mark the holder "carrying a flee flag" (the |= 4 bit). Modeled as the
            // banner + flag-attach below; the bit itself is a render flag.
            bool found = false;
            u16 nameIndex = g_findPerson(newHolder->unitId, found);
            g_banner(kBannerConquerFlagTextId, nameIndex);
        }
    }

    // if ( v3 != *(__int16 **)(v4 + 512) )   // the holder changed
    i32 currentFlagMesh = anchor ? anchor->flagMesh : 0;
    bool holderChanged = (newHolder != anchor) || (newHolder && currentFlagMesh == 0)
                       || (newHolder == nullptr && currentFlagMesh != 0);
    // The original compares v3 (new holder) against record+512 (the last holder);
    // we approximate "did the anchor's flag holder change" by whether a holder is
    // present vs. a mesh already exists.
    if (newHolder == nullptr) {
        // if ( !v3 ) { *(v4+516)=0; *(v4+512)=0; return; }  // clear, no new flag.
        if (anchor && anchor->flagMesh) {
            g_flagDetach(anchor->flagMesh);
            ++g_log.flagDetachCount; // mirror the explicit release before clear
            anchor->flagMesh = 0;
        }
        return false;
    }

    (void)holderChanged;
    // if ( *(_DWORD *)(v4 + 516) ) { DetachAndRelease(v4+516); v4+516 = 0; }
    if (newHolder->flagMesh) {
        g_flagDetach(newHolder->flagMesh);
        newHolder->flagMesh = 0;
    }

    // Attach a fresh flee-flag mesh; the personType selects the texture set (the
    // SelectTextureSet(... *(byte)(v8+84)-61 ...) branch) — modeled via the hook.
    newHolder->flagMesh = g_flagAttach(newHolder->unitId, newHolder->personType);
    return newHolder->flagMesh != 0;
}

// ===========================================================================
// gilde.exe 0x49CD10 — VIBE_Character_AttachToScene.
// ===========================================================================
void CharacterAttachToScene(i32 sceneObject, const float curPos[3], const float curDir[3],
                            const float targetPos[3], const float targetDir[3]) {
    // if ( result ) { v5 = *(_DWORD *)(result + 388);  if ( v5 ) { ... } }
    // Modeled: the actor's scene object handle (0 == no scene node -> no-op).
    if (sceneObject == 0)
        return;

    // if ( !VectorWithinTolerance(scenePos, targetPos, 8.0) )
    //     VIBE_Object_SetPosition(scenePtr, targetPos);
    if (!VectorWithinTolerance(curPos, targetPos, 8.0f))
        g_scenePos(sceneObject, targetPos);

    // result = VectorWithinTolerance(sceneDir, targetDir, 8.0);
    // if ( !result ) VIBE_Object_SetWorldTranslation(scenePtr, targetDir);
    if (!VectorWithinTolerance(curDir, targetDir, 8.0f))
        g_sceneTrans(sceneObject, targetDir);
    // *(_DWORD *)(v3 + 36) = v6;  // an order-phase scratch field (render-side).
}

} // namespace guild::sim
