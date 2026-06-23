#pragma once
// menu_actor — the dynasty-scene "menu dummy actor" factory from gilde.exe.
//
//   VIBE_Character_CreateMenuDummyActor @0x52af64 — instantiates ONE animated ancestor
//   actor in the dynasty/ancestry scene. VIBE_Menu_RunChooseCharacter @0x52bcd4 calls
//   this nine times (one per clickable ancestor), each bound to a named scene "dummy"
//   node and a model. The function finds the dummy scene node by name, samples its world
//   position, creates a character actor from the model, faces the actor along the dummy's
//   forward direction, configures the actor record (scale 0.6666667, the dynasty
//   "unused" slot marker -1, active flag, the type-4 category byte, the back-reference on
//   its object node), preloads the "bewegung/gehen" gait anim set, and registers a status
//   text. Returns the new character actor handle (or null when the dummy/character could
//   not be created).
//
// The function itself is pure control flow + a small amount of vector math; that math is
// already reconstructed in guild::util and is REUSED here (VectorAngleBetween @0x5ca334,
// RotateVectorByHierarchy @0x5c8990, PointThroughBoneChain @0x5c8b38). The genuine
// scene-graph / character-subsystem leaves it touches (object lookup by handle, character
// creation from a model, the object-node world rotation / dirty-flag / status-text
// registration, terrain query, the gait preload) live in OTHER, largely unreconstructed
// modules; they are routed through an installable MenuActorHooks dispatch table whose
// default implementation is inert. This mirrors the CharRender3Hooks pattern in
// sim/character_render3.{h,cpp}. A recording mock makes the whole body golden-testable.
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Recovered constants.
//   flt_5CA2B0       == {0, 0, 1}        : the +Z forward reference vector.
//   aBewegungGehen   == "bewegung/gehen" : the gait anim set preloaded (count 1).
//   1060320051       == 0x3F2AAAB3       : float scale 0.6666667f  (actor +416).
//   actor +508 = -1  : dynasty "unused slot" marker (RunChooseCharacter checks ==-1).
//   actor +44  = 1   : active flag.
//   char-record +512 = 4 (byte) : type/category.
//   object-node +512 = &char     : back-reference to the char record.
//   object-node +72  = 0         : cleared bookkeeping word.
// ===========================================================================
constexpr float kMenuActorScale = 0.6666667f;   // 0x3F2AAAB3 == 1060320051
extern const float kForwardZ[3];                // flt_5CA2B0 == {0,0,1}
constexpr const char kBewegungGehen[] = "bewegung/gehen";  // aBewegungGehen

// ===========================================================================
// Cross-module dispatch hooks. Each genuine scene-graph / character-subsystem call the
// original makes is a slot here; the default table is inert so the control flow and the
// (reconstructed) facing math are testable in isolation. Tests install a recording mock.
//
// Handles are opaque pointers here; the original stores 32-bit handles.
// ===========================================================================
struct MenuActorHooks {
    // VIBE_Object_FindByHandle(0, 256, name, 0, model) @0x5b7be4 — locate the scene
    // dummy node by name (the actor's anchor). Returns the dummy node, or null.
    void* (*findDummyObject)(int name, const char* model);
    // VIBE_Transform_PointThroughBoneChain(dummy, dummy+19, out3) @0x5c8b38 — sample the
    // dummy's world position. (Computed for parity / placement; the original keeps it in
    // a local. We expose it so the record can capture the placement point.)
    void  (*getDummyWorldPos)(void* dummy, float out3[3]);
    // VIBE_Character_CreateFromModel(model) @0x402d10 — create a character actor from the
    // model name. Returns the new char record, or null.
    void* (*createCharacterFromModel)(const char* model);
    // The char's renderable object/scene node, *(char+52).
    void* (*charObjectNode)(void* chr);
    // VIBE_Object_SetWorldTranslation(node, yawVec3) @0x5af50c — set the object node's
    // world rotation to {0, yaw, 0}.
    void  (*setWorldRotation)(void* node, const float yawVec3[3]);
    // VIBE_Character_QueryTerrainType(chr, 0) @0x404650.
    void  (*queryTerrainType)(void* chr);
    // *(node+512) = chr — back-reference the char record on its object node.
    void  (*linkNodeBackref)(void* node, void* chr);
    // VIBE_Object_PropagateDirtyFlag(node, 1) @0x5af2c0.
    void  (*propagateDirtyFlag)(void* node);
    // VIBE_Character_PreloadAniSet(chr, 1, "bewegung/gehen") @0x403c34.
    void  (*preloadGaitAnim)(void* chr);
    // VIBE_StatusText_Register(node, 0) @0x4bcc80.
    void  (*registerStatusText)(void* node);
};
void SetMenuActorHooks(const MenuActorHooks* hooks);
const MenuActorHooks& GetMenuActorHooks();

// ===========================================================================
// MenuActorRecord — captures what CreateMenuDummyActor did, for headless assertions.
// (Pure observation; the original keeps none of this — it just mutates the records.)
// ===========================================================================
struct MenuActorRecord {
    bool  found        = false;   // the dummy scene node was located
    bool  created      = false;   // a character actor was created from the model
    float placedPos[3] = {0, 0, 0};  // dummy world position (PointThroughBoneChain)
    float facingYaw    = 0.0f;    // VectorAngleBetween((0,0,1), Rotate(dummy,(0,0,1)))
    float scale        = 0.0f;    // actor +416   (0.6666667 on success)
    int   slotMarker   = 0;       // actor +508   (-1 on success)
    int   typeByte     = 0;       // char-record +512 (4 on success)
    int   active       = 0;       // actor +44    (1 on success)
    bool  gaitPreloaded= false;   // PreloadAniSet("bewegung/gehen") was issued
    bool  backrefLinked= false;   // node +512 set to the char record
};

// gilde.exe 0x52af64 — VIBE_Character_CreateMenuDummyActor (__usercall, eax=fn(name@eax,
// model@edx)). Returns the created character actor (or null when the dummy node wasn't
// found / the character couldn't be created). `rec` (optional) captures the observations.
void* CreateMenuDummyActor(int dummyName, const char* model, MenuActorRecord* rec = nullptr);

} // namespace guild::sim
