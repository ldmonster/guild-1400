#pragma once
// character_recon5_spawn — 1:1 reconstruction of the VIBE_Character spawn
// orchestration cluster of gilde.exe.
//
// Reconstructed here (byte-faithful control flow + orchestration):
//   VIBE_Character_PreloadSceneAnimations  0x50650c  (__usercall eax)
//   VIBE_Character_SpawnOfficeStaffActor   0x57c744  (__userpurge)
//   VIBE_Character_SpawnAtBuildingEntrance 0x57c8f0  (__userpurge)
//
// These pick a model, create the actor from it (CreateFromModel), transform a
// spawn point through the bone chain, decide which avatar slot owns it
// (EnsureBuildingAvatar / EnsureObjectAvatar — already reconstructed in
// character_recon4_avatar), and copy the head-variant string.  The create /
// bone-transform / object leaves are coupled to not-yet-reconstructed
// subsystems and routed through SpawnHooks as INERT-DEFAULT hooks.  The
// orchestration (the decision tree, the defaults selection, the 2-byte
// NUL-terminated copy loop, the preload-table walk) is reproduced exactly.
//
// EnsureBuildingAvatar / EnsureObjectAvatar bookkeeping lives in
// character_recon4_avatar.{h,cpp}; this module reaches them through hooks so it
// does not duplicate (ODR) that logic.
//
// No third-party tech introduced (rule 6 N/A).
#include "guild/common/types.h"
#include "character_recon5_transport.h"   // TObject / f32

namespace guild::sim {

// Per-person actor record (the unsigned __int16* a1 in SpawnOfficeStaffActor).
// Only the fields the spawn path reads/writes are modelled:
//   *a1            (u16)  : object type / staff-model key   ("*a1")
//   a1[1] (dword)         : person id (used in "sp_%i")     (*((DWORD*)a1+1))
//   a1[97](dword)         : the created character handle     (*((DWORD*)a1+97))
//   a1+248               : head-variant string buffer (NUL-terminated copy dst)
struct ActorRec {
    u16  modelKey   = 0;     // *a1
    int  personId   = 0;     // a1[1]
    int  characterHandle = 0;// a1[97] (created character)
    char headVariant[64] = {0};  // a1+248 region (dst of the copy loop)
};

// Staff-model definition row resolved by VIBE_Office_ResolveStaffModel.
//   +0  (skipped header) ; +4..  : model name string used by CreateFromModel and
//   copied into the actor's head-variant buffer (2 bytes at a time).
struct StaffModelDef {
    char name[64] = {0};     // model name (read from +4 in the binary)
};

struct SpawnHooks {
    // VIBE_Universe_SwitchActiveSlot 0x5b4a24 — make universe `u` active; returns
    // the previously-active slot (restored at the end).
    void (*switchActiveSlot)(int universe, int a2, int prev, int a4) = nullptr;
    // VIBE_Object_FindByHandle 0x5b7be4 — resolve an object handle to a pointer.
    TObject* (*findByHandle)(void* ctx, int kind, int handle, int a4, int a5) = nullptr;
    // VIBE_Transform_PointThroughBoneChain 0x5c8b38 — transform obj.pos (+76)
    // through the object's bone chain into out[3].
    void (*pointThroughBoneChain)(TObject* obj, const f32* in, f32* out) = nullptr;
    // VIBE_Office_ResolveStaffModel 0x57c1e8 — resolve a staff-model def for key.
    StaffModelDef* (*resolveStaffModel)(u16 modelKey) = nullptr;
    // VIBE_Character_CreateFromModel 0x402d10 — instantiate a character; returns handle.
    int (*createFromModel)(const char* modelName, int universe) = nullptr;
    // VIBE_Object_SetWorldTranslation 0x5af50c — push a world-translation block.
    void (*setWorldTranslation)(int objPtr, const f32* wtrans) = nullptr;
    // VIBE_Character_ApplyHeadVariant 0x57c548 / VIBE_Character_ResolveHeadBone 0x57c5d4
    void (*applyHeadVariant)(ActorRec* a) = nullptr;
    void (*resolveHeadBone)(ActorRec* a, int v18) = nullptr;
    void* ctx = nullptr;
    int   prevActiveSlot = 0;   // dword_649D60 — the restore target
};

// Default attach pose used when the caller passes no position / rotation.
// dword_577A68 (4 dwords) and dword_577A78 (3 dwords) are both all-zero in the
// binary; modelled as zero vectors.
constexpr f32 kDefaultSpawnPos[4]  = {0,0,0,0};   // dword_577A68
constexpr f32 kDefaultSpawnRot[3]  = {0,0,0};     // dword_577A78

// ---------------------------------------------------------------------------
// gilde.exe 0x57c744 — VIBE_Character_SpawnOfficeStaffActor
//   actor   = a1   (ActorRec*)
//   uni     = a2   (target universe; <0 -> return 0)
//   rotPtr  = a3   (optional world-rotation, defaults to dword_577A78)
//   posPtr  = a4   (optional position, defaults to dword_577A68)
//   findCtx = a5   (object-find context)
//   findHandle = a6 (object handle to resolve a spawn anchor through, or 0)
//   Switches to `uni`, resolves an optional anchor object (and its bone-chain
//   point), resolves the staff model, creates the character from it, names it
//   "sp_%i", sets the object type word, pushes the rotation, copies the model
//   name into the head-variant buffer, applies the head variant/bone, then
//   restores the previous active slot.  Returns the created character handle.
// ---------------------------------------------------------------------------
int SpawnOfficeStaffActor(ActorRec* actor, int uni, const f32* rotPtr, const f32* posPtr,
                          void* findCtx, int findHandle, SpawnHooks& H);

// Hooks for SpawnAtBuildingEntrance (orchestration over person/building queries
// and the avatar-ensure leaves, which are reconstructed in recon4).
struct EntranceHooks {
    // VIBE_Person_FindRecordById 0x58bc6c — person record for an id (0 if none).
    ActorRec* (*findPersonById)(int personId) = nullptr;
    // VIBE_Person_QueryBegin 0x586c20 — begin a person query for the building.
    int (*personQueryBegin)(ActorRec* p, int a2, int a3, int building) = nullptr;
    // VIBE_GameObject_QueryFind 0x5857fc — resolve an object query.
    int (*gameObjectQueryFind)(int a1, int a2, int a3, int obj) = nullptr;
    // VIBE_Building_IsProductionType 0x587f80
    int (*isProductionType)(int rec) = nullptr;
    // EnsureBuildingAvatar 0x505134 / EnsureObjectAvatar 0x505074 (recon4)
    int (*ensureBuildingAvatar)(int rec, int building, i16 a3) = nullptr;
    int (*ensureObjectAvatar)(int objRec) = nullptr;
    // VIBE_Character_PickWaitAnimation 0x406344 — choose a wait anim name (0=none).
    const char* (*pickWaitAnimation)(int rec) = nullptr;
    // VIBE_Character_PreloadAniSet 0x403c34 — preload "bewegung/gehen".
    void (*preloadAniSet)(int handle, int n, const char* name) = nullptr;
    // VIBE_Character_SetVisible 0x401894
    void (*setVisible)(int handle, int visible) = nullptr;
    // The two "special interior" pointers (dword_631744 / dword_631748): a
    // building matching either is left visible.  Modelled as record ids; 0 == none.
    int special1 = 0;   // dword_631744
    int special2 = 0;   // dword_631748
    SpawnHooks* spawn = nullptr;   // forwarded to SpawnOfficeStaffActor
    void* ctx = nullptr;
};

// ---------------------------------------------------------------------------
// gilde.exe 0x57c8f0 — VIBE_Character_SpawnAtBuildingEntrance
//   personId  = a4  (the person to spawn)
//   building  = a1  (building universe / id)
//   objHandle = a3  (-1 == none)
//   posPtr    = a2  (optional position)
//   rotPtr    = a5  (optional rotation; u16*)
//   Resolves the person (must not already have a character), queries the
//   building, decides the owning avatar slot (production -> building avatar,
//   else object avatar), spawns the staff actor at the entrance (a queue object
//   chooses "dummy_EINGANG"/a wait anim, otherwise "dummy_TUER"), preloads the
//   walk anim, stores building/object ids, and hides the actor unless its
//   building is one of the two "special interior" buildings.
//   Returns the created character handle (or 0 / null on any failure).
// ---------------------------------------------------------------------------
int SpawnAtBuildingEntrance(int personId, int building, int objHandle,
                            const f32* posPtr, const u16* rotPtr, EntranceHooks& H);

// ===========================================================================
// PreloadSceneAnimations table layout.
// dword_13CE294 + 589*type  : building-name row (the actor's type *a1 selects it).
// byte_6344A4 / dword_6344A0 : a 1729-byte-stride table of "scene anim sets",
//   keyed by a type byte in the high byte of the dword at (dword_6344A0+1+v2);
//   walked until the keyed byte matches *v3 or a terminator byte is zero.
//   Once located, 18 sub-string slices (offsets +1,+49,+97,...+721, stride 48)
//   are passed to PreloadAniSet for every scene object whose type matches.
//   word_12CE910 .. byte_1333110 : the scene-object table (stride 268 words =
//   536 bytes), each entry's [+97] dword is the object pointer and [+91] its key.
// This is pure table-walk orchestration; the actual preload is a hook.
// ===========================================================================
struct PreloadHooks {
    // VIBE_Character_CollectByOwner 0x4b99ac — gather owned characters for actor.
    void (*collectByOwner)(int a1, int a2) = nullptr;
    // VIBE_Character_PreloadAniSet 0x403c34 — preload one scene-object's anim set
    // (18 name slices).  Here reduced to a callback recording the object key.
    void (*preloadAniSet)(int objPtr, int n, const char* const* slices, int sliceCount) = nullptr;
    void* ctx = nullptr;
};

// A scene-object row (only the two fields the walk reads).
struct SceneObject {
    int objPtr = 0;   // [+97] dword
    int key    = 0;   // [+91] dword
    int matchVal = 0; // *(objPtr+44) compared against *(v7+1)
};

// ---------------------------------------------------------------------------
// gilde.exe 0x50650c — VIBE_Character_PreloadSceneAnimations(actorType)
//   Look up the anim-set row for the actor's type, collect owned characters,
//   then for every scene object whose key matches, preload its 18-slice anim set.
//   Reconstructs the table-walk orchestration; the per-object preload is a hook.
//   `actorTypeByte` = *a1 ; `sceneObjects`/`count` model the scene-object table;
//   `setKey`/`setMatch` model the located anim-set row's key fields.
// ---------------------------------------------------------------------------
void PreloadSceneAnimations(int actorTypeByte, SceneObject* sceneObjects, int count,
                            int setKey, int setMatch, PreloadHooks& H);

} // namespace guild::sim
