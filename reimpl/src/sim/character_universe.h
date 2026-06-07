#pragma once
// character_universe — the FULL 1:1 translation of the character universe/scene
// relocation action for the Guild simulation (gilde.exe). This complements the
// abstracted relocation in character_move.{h,cpp}: here the engine's exact
// validation ladder, field-clear order, texture-set/visibility-mismatch logic and
// the entry-dummy transform are reproduced against the byte offsets of the
// Character record and the action node.
//
// Translated functions:
//   VIBE_Character_Move2UniverseActionUpdate  0x4063c8  (action type 51)
//
// The relocation moves a character from one scene/universe to another (e.g.
// entering a building / a town gate). The genuine render/universe leaves
// (MoveToUniverse object reparent, SwitchActiveSlot, SelectTextureSet, the
// "dummy_EINGANG" entry-dummy bone transform, SetVisible) are out of this module's
// scope and routed through UniverseHooks; the control flow + field bookkeeping is
// translated faithfully from the Hex-Rays reference of record.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Universe-record stride (byte_13ECEC8 @0x13ECEC8): the engine addresses the
// destination universe as &byte_13ECEC8[984 * Data[0]] — a 984-byte stride array
// of universe/scene records. Data[0] is the destination universe index.
// ===========================================================================
constexpr int kUniverseRecordStride = 984;   // 0x3D8

// ===========================================================================
// Character fields the relocation writes (32-bit Character record; the action
// node `a1` IS the Character base in the engine — the action data lives at the
// node arg offsets +48/+52/+56, which alias the character's +48/+52/+56 scratch):
//   action node args (the "Action->Data"):
//     +48 (Data[0]) destination universe index
//     +52 (Data[1]) destination id-in-universe (-1 == CH_ID_NONE)
//     +56 (Data[2]) destination sub-id
//     +40 (ptr)     the next/owning action node (node+9 == type; 56 -> hide)
//     +240 (str)    optional entry-dummy name override (else "dummy_EINGANG")
//     +380 (byte)   "skip entry transform" flag
//   character (the relocated actor, v26 == ch+20 avatar's owner):
//     +44  (dword)  current universe id ([11])   <- Data[1]
//     +48  (dword)  ([12])                        <- Data[2]
//     +56  (dword)  ([14]) cleared to 0
//     +60  (dword)  ([15]) cleared to 0
//     +64  (dword)  ([16]) cleared to 0
//     +506 (byte)   indoor flag (texture-set select arg when in active universe)
// ===========================================================================
struct UniverseTransition {
    // --- action node data (Action->Data) ---
    int dataUniverse;   // node +48  (Data[0]) destination universe index
    int dataId;         // node +52  (Data[1]) destination id (-1 none)
    int dataSubId;      // node +56  (Data[2]) destination sub-id
    bool nextIsHide;    // node +40 && node+40[9] == 56  -> hide after move
    bool skipEntry;     // node +380 != 0  -> skip the entry-dummy transform
    const char* entryDummy;  // node +240 (or "dummy_EINGANG" if empty)

    // --- character (the relocated actor) ---
    int curUniverse;    // character +44 (dword [11]) current universe id
    int char48;         // character +48 (dword [12])
    int char56;         // character +56 ([14])
    int char60;         // character +60 ([15])
    int char64;         // character +64 ([16])
    u8  indoorFlag;     // character +506 (texture-set arg)
    bool inActiveUniverse;  // character +44 == g_activeUniverse (dword_62D080)

    // --- result scratch ---
    int  moveResult;    // MoveToUniverse leaf result (0 == failed)
    int  prevSlot;      // saved active slot (dword_649D60)
    int  visible;       // mirror of the visibility request (last SetVisible arg)
    bool entryFound;    // entry-dummy object resolved (bone transform applied)
};

// ===========================================================================
// Active-slot globals the relocation reads:
//   dword_649D60 @0x649D60  active scene slot (saved/restored around the move)
//   dword_62D080 @0x62D080  active-universe id (== ch+44 -> in active universe)
//   dword_62D084 @0x62D084  active sub-universe id (visibility-mismatch check)
// ===========================================================================
extern int g_univActiveSlot;        // dword_649D60
extern int g_univActiveUniverseId;  // dword_62D080
extern int g_univActiveSubUniverse; // dword_62D084

// ===========================================================================
// Hooks for the render/universe leaves of the relocation.
// ===========================================================================
struct UniverseHooks {
    // VIBE_Character_MoveToUniverse(ch, &universe[984*idx]): reparent the
    // character's scene objects into the destination universe record; returns
    // nonzero on success. `universeIndex` is Data[0].
    int (*moveToUniverse)(UniverseTransition* st, int universeIndex);
    // VIBE_Universe_SwitchActiveSlot(slot): make `slot` the active scene slot.
    void (*switchActiveSlot)(int slot);
    // VIBE_Object_SelectTextureSet: pick the universe-appropriate texture set.
    // `outer` == 1 when moving out of the active universe (low-poly set);
    // `indoor` == ch+506 when in the active universe.
    void (*selectTextureSet)(UniverseTransition* st, int outer, u8 indoor);
    // VIBE_Character_StopSample(ch): stop any looping sample on the actor.
    void (*stopSample)(UniverseTransition* st);
    // Resolve the entry-dummy object ("dummy_EINGANG" or the override) and apply
    // its bone-chain transform as the character's spawn point. Returns nonzero if
    // the dummy was found (ApplyVisibilityState then runs); 0 -> skip placement.
    int (*placeAtEntryDummy)(UniverseTransition* st, const char* name);
    // VIBE_Character_SetVisible(ch, visible): toggle the actor's visibility.
    void (*setVisible)(UniverseTransition* st, int visible);
};
void SetUniverseHooks(const UniverseHooks* hooks);
const UniverseHooks& GetUniverseHooks();

// Classification of the relocation step result.
enum class UniverseResult {
    kInvalidUniverse,   // Data[0] < 0                       (logged + unlink)
    kInvalidCombo,      // (Data0==0) != (Data1==-1)         (logged + unlink)
    kMoveFailed,        // MoveToUniverse leaf failed        (logged + unlink)
    kOk,                // relocated                          (unlink)
};

// gilde.exe 0x4063c8 — VIBE_Character_Move2UniverseActionUpdate (action type 51).
// Full body: validates (universe,id), performs the reparent via the hook, switches
// the active scene slot, selects the texture set, stops samples, places the actor
// at the entry dummy, clears the move scratch (+56/+60/+64), sets the new universe
// id (+44 = Data[1]) and sub-id (+48 = Data[2]), toggles visibility (including the
// slot-mismatch and "next action == hide(56)" cases), restores the previous slot,
// and returns the classification (the caller unlinks the node on every path).
UniverseResult Move2UniverseActionUpdate(UniverseTransition* st);

} // namespace guild::sim
