#pragma once
#include "guild/common/types.h"

#include <functional>

// gilde.exe — Command unit-ORDER execution leaves (namespace guild::sim).
// MODULE: sim / Command. These are the three VIBE_Command_Exec* handlers reached
// from VIBE_Command_ExDispatchUnitOrder @0x49CDA0 (opcode 0x55, already in
// command_apply3.cpp). That dispatcher gates on the active battle and a per-unit
// "alive" byte, then routes the order code byte to one of seven gestures; three
// of those gestures are these heavier Exec* leaves (the other four — celebrate /
// attack / pick-up / stand-up — live in combat_orders.* / combat_action.cpp):
//
//   order 3  -> VIBE_Command_ExecConquerFlag      @0x491148  attach a flee-flag
//   order 5  -> VIBE_Command_ExecPickupGroundItem @0x491054  pick item off ground
//   order 7  -> VIBE_Command_ExecUnitSelectSound  @0x49110C  unit-selected banner
//
// Plus the small scene-sync leaf the dispatcher calls before orders 2/3/6:
//   VIBE_Character_AttachToScene @0x49CD10 — snap the unit's live scene object to
//   the order's target position/orientation (within an 8.0 tolerance).
//
// All four touch the deep render / scene-object / character-actor clusters
// (VIBE_Object_*, VIBE_Character_*, VIBE_Text_*/Hud_*) that other agents own or
// have deferred. Following the command_apply4 convention, those leaves are routed
// through mockable function-pointer hooks with faithful default backends that
// model just the observable record mutation / status banner. The pure decision
// logic (the alive/handle gates, the order-code switch, the position-tolerance
// rule, the action tag, the banner text id) is translated 1:1.

namespace guild::sim {

// VIBE_Math_VectorWithinTolerance @0x5caa4c — REUSED (one definition, owned by
// character_social.cpp). Forward-declared here (not redefined) per the ODR rule;
// AttachToScene calls it. Signature matches character_social.h exactly.
bool VectorWithinTolerance(const float a[3], const float b[3], float tol);

// ---------------------------------------------------------------------------
// The order sub-record the dispatcher hands each Exec* leaf (packet payload at
// a1+53 in VIBE_Command_ExDispatchUnitOrder). Only the fields the three leaves
// and AttachToScene actually read are modeled, at their original byte offsets.
// ---------------------------------------------------------------------------
struct UnitOrderRecord {
    i32 unitId;       // +0x00  acting unit id (FindUnitById key; -1 == none)
    u8  active;       // +0x04  (a1+4) nonzero == order live; gate for 3/5
    // +0x05..+0x24 order payload (the target position lives in the +37 region the
    // dispatcher passes to AttachToScene). We expose the resolved target here.
    float targetPos[3];   // resolved order target world position (v6+37 region)
    float targetDir[3];   // resolved order target facing (the second vector)
};

// ---------------------------------------------------------------------------
// Modeled live combat unit (the subset the leaves dereference off FindUnitById).
// In the binary this is word_B5A350[...] (536-byte stride). Here it is a small
// modeled record a test seeds; the hooks resolve a unit id to one of these.
// ---------------------------------------------------------------------------
struct UnitOrderActor {
    i32  unitId       = -1;  // unit[1] id (the FindUnitById key)
    i32  actorPtr     = 0;   // unit[97] (+388) character-actor handle (0 == none)
    i32  carriedObject = 0;  // unit[107] (+428) handle of a carried scene object
    u8   personType   = 0;   // unit[2] head byte gating the conquer-flag texture
    i32  flagMesh     = 0;   // (record+512/+516) the unit's attached flee flag
};

// ---------------------------------------------------------------------------
// Observable record of what the four leaves did (for verification). Reset by
// ResetUnitOrderState.
// ---------------------------------------------------------------------------
struct UnitOrderLog {
    // ExecPickupGroundItem
    int  standUpCount      = 0;   // Character_StandUp calls
    i32  lastStandUpActor  = 0;
    int  releaseCarriedCount = 0; // carried-object detach (unit[107] != 0 path)
    int  pickupActionCount = 0;   // "einhaendig_vom_boden_nehmen" action inserted
    i32  lastPickupActor   = 0;
    // ExecUnitSelectSound
    int  bannerCount       = 0;   // Hud_SetStatusBannerText calls
    int  lastBannerTextId  = 0;   // the format-string id (3610)
    i32  lastBannerPerson  = 0;   // the person record the banner named
    // ExecConquerFlag
    int  flagAttachCount   = 0;   // a flee-flag mesh was (re)attached to a unit
    int  flagDetachCount   = 0;   // a previous flag mesh was released
    i32  lastFlagUnit      = 0;
    // AttachToScene
    int  scenePosUpdates   = 0;   // Object_SetPosition (position out of tolerance)
    int  sceneDirUpdates   = 0;   // Object_SetWorldTranslation (facing out of tol)
};
const UnitOrderLog& UnitOrderLogRef();

// ---------------------------------------------------------------------------
// Leaf hooks (deep render / scene / person / text leaves). Defaults model the
// observable mutation + log entry so the order logic round-trips in isolation.
// ---------------------------------------------------------------------------

// VIBE_Combat_FindUnitById @0x486430 — resolve a unit id to its live actor, or
// null. Default: linear scan of the seeded unit set (see SeedUnit).
using FindUnitFn = UnitOrderActor* (*)(i32 unitId);
void SetFindUnitHook(FindUnitFn fn);

// VIBE_Character_StandUp @0x405504 — cancel any "sitting" action on an actor.
using StandUpFn = void (*)(i32 actorPtr);
void SetStandUpHook(StandUpFn fn);

// VIBE_Object_DetachAndRelease @0x5b4258 (via ApplyParentTransform + suspend) —
// release the unit's carried ground object back to the world. The default
// records the release; returns nonzero on success.
using ReleaseCarriedFn = int (*)(i32 carriedHandle, i32 actorPtr);
void SetReleaseCarriedHook(ReleaseCarriedFn fn);

// VIBE_CharAction_InsertActionVararg @0x40c1e4 — queue a named character action
// (tag, name) on an actor. Default records it.
using InsertActionFn = void (*)(i32 actorPtr, int tag, const char* name);
void SetInsertActionHook(InsertActionFn fn);

// VIBE_Person_FindRecordById @0x58bc6c — resolve a person id to its record; we
// only need the record's leading u16 (the name index the banner formats).
using FindPersonNameFn = u16 (*)(i32 personId, bool& found);
void SetFindPersonNameHook(FindPersonNameFn fn);

// VIBE_Hud_SetStatusBannerText @0x4bcdcc via VIBE_Text_RenderFormattedMessage
// @0x59f99c — show a formatted status banner. Default records (textId, nameIdx).
using BannerFn = void (*)(int textId, u16 nameIndex);
void SetBannerHook(BannerFn fn);

// The conquer-flag mesh attach/detach (VIBE_Object_AttachToUniverseNode @0x5b3e30
// + Character_LoadObjectAnimation @0x426488 chain). Default records the (re)attach
// and returns a synthetic mesh handle (>0). detach releases a prior handle.
using FlagAttachFn = i32 (*)(i32 unitId, u8 personType);
void SetFlagAttachHook(FlagAttachFn fn);
using FlagDetachFn = void (*)(i32 meshHandle);
void SetFlagDetachHook(FlagDetachFn fn);

// VIBE_Object_SetPosition @0x5af38c / VIBE_Object_SetWorldTranslation @0x5af50c —
// the two scene writes AttachToScene performs. Default just logs.
using SceneSetVecFn = void (*)(i32 sceneObject, const float v[3]);
void SetScenePositionHook(SceneSetVecFn fn);
void SetSceneTranslationHook(SceneSetVecFn fn);

// Seed / reset the modeled unit set + hooks + log (test helper).
UnitOrderActor* SeedUnit(const UnitOrderActor& u); // returns the stored slot
void ResetUnitOrderState();

// ===========================================================================
// The order-execution leaves.
// ===========================================================================

// gilde.exe 0x491054 — VIBE_Command_ExecPickupGroundItem.
//   if (rec.active && rec.unitId != -1) {
//     u = FindUnitById(rec.unitId); Character_StandUp(u.actorPtr);
//     if (u.carriedObject) { release it; u.carriedObject = 0; }
//     InsertAction(u.actorPtr, tag 46, "spezial/einhaendig_vom_boden_nehmen"); }
// Returns the gate's last byte (the original returns al); we surface the gate.
bool ExecPickupGroundItem(const UnitOrderRecord& rec);

// gilde.exe 0x49110C — VIBE_Command_ExecUnitSelectSound.
//   p = FindPersonRecord(rec.unitId);  (latched into dword_6311F0)
//   banner = Text(3610, *(u16*)p);  Hud_SetStatusBannerText(banner);
// Returns whether the person was found (banner shown).
bool ExecUnitSelectSound(const UnitOrderRecord& rec);

// gilde.exe 0x491148 — VIBE_Command_ExecConquerFlag.
//   if (!rec.active) return; resolve the flag-anchor unit (rec.unitId, may be -1):
//     mark that unit "carrying a flee flag" (u.flagState |= 4) + banner 3611;
//   if the anchor differs from the unit currently holding this flag:
//     release the old flag mesh; if no new anchor, clear + return;
//     else attach a new flee-flag mesh (texture chosen by personType) and store it.
// Returns whether a flag mesh ended up attached to a unit.
bool ExecConquerFlag(const UnitOrderRecord& rec);

// gilde.exe 0x49CD10 — VIBE_Character_AttachToScene.
//   if (actor && actor.scenePtr) {
//     if (!within(scenePos, pos, 8)) SetPosition(scenePtr, pos);
//     if (!within(sceneDir, dir, 8)) SetWorldTranslation(scenePtr, dir);
//     actor.orderFlag = 1; }
// Models the two scene writes; `sceneObject` is the actor's +388->+52 scene ptr
// (0 == none, no-op). curPos/curDir are its current scene transform.
void CharacterAttachToScene(i32 sceneObject, const float curPos[3], const float curDir[3],
                            const float targetPos[3], const float targetDir[3]);

} // namespace guild::sim
