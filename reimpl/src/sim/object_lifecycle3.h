#pragma once
// ===========================================================================
// object_lifecycle3.{h,cpp} — VIBE_Object_* scene-graph transform thunks,
// destroy/teardown, find-by-handle/name, door/type classifiers (batch 3).
// namespace guild::sim.
// ===========================================================================
// MODULE: the remaining untranslated VIBE_Object_* leaves that operate on the
// heap scene-GRAPH node (the ~0x21C-byte "d3:SpawnObject" block). batch 1/2
// (object.cpp / objectsearch.cpp / object_lifecycle2.cpp) covered the
// gameplay-RECORD and flag/fill mutators; this batch covers:
//
//   * the transform setter family (position / scale / pivot / world-translation
//     vector setters + their XYZ thunks) and the dirty-flag invalidation core,
//   * the command-layer object mutators (KillObject / SetPos / MoveObject /
//     ReplaceObject / the rain-effect state thunks),
//   * the scene-graph find-by-handle / find-by-name walk callbacks + drivers,
//   * the hidden-state toggle and the id/door/type classifiers.
//
// Functions that bottom out in the render scene-graph walker
// (VIBE_SceneGraph_WalkAndInvoke / _TraverseTree), the shadow/math/sound/floor
// leaves, and the script-error reporter are routed through ObjLife3Hooks (inert
// defaults installed by this .cpp) so the node-field arithmetic is exact and
// independently testable. The transform setters' faithful 1:1 part is the
// dword-indexed write into the node; the walk/traverse/shadow calls are hooks.
//
// The scene-graph node is the SAME block modeled as guild::sim::SceneObject in
// object_lifecycle2.h, but THIS family reads it as a flat dword array (a1[19],
// a1[27], ...). To stay 1:1 with that access pattern without colliding with the
// named SceneObject view, we model it here as SceneNode3 — a union of named
// vectors at the recovered byte offsets and a raw dword array. Offsets on every
// field.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x5af2e4  VIBE_Object_InvalidateCurrent        (dirty current + sky/floor)
//   0x5af3ec  VIBE_Object_SetPositionXYZ           (thunk -> SetPosition)
//   0x5af38c  VIBE_Object_SetPosition              (write +76 pos, dirty)
//   0x5af418  VIBE_Object_SetScaleVector           (write +108 scale, dirty)
//   0x5af464  VIBE_Object_SetScaleVectorXYZ        (thunk -> SetScaleVector)
//   0x5af490  VIBE_Object_SetPivotVector           (write +120 pivot, dirty)
//   0x5af4e0  VIBE_Object_SetPivotVectorXYZ        (thunk -> SetPivotVector)
//   0x5af50c  VIBE_Object_SetWorldTranslation      (write +132 euler, matrix)
//   0x5af5cc  VIBE_Object_SetWorldTranslationXYZ   (thunk -> SetWorldTranslation)
//   0x43e724  VIBE_Object_KillObject               (detach-and-release / error)
//   0x43e758  VIBE_Object_SetPos                   (cmd: pos swap, SetPosition)
//   0x43e804  VIBE_Object_MoveObject               (cmd: 3d-sound move)
//   0x43ea48  VIBE_Object_ReplaceObject            (cmd: rebind parent mesh)
//   0x43f844  VIBE_Object_CmdSetObjectStateThunk   (rain create)
//   0x43f84c  VIBE_Object_CmdResetObjectThunk      (rain destroy)
//   0x5b3698  VIBE_Object_ToggleHiddenState        (nodeType 5<->6, dirty)
//   0x583a70  VIBE_Object_FindObjectById           (linear id scan, stride 67)
//   0x5b7b7c  VIBE_Object_MatchHandleCallback      (handle-match walk cb)
//   0x5b7be4  VIBE_Object_FindByHandle             (walk for handle match)
//   0x5b7c48  VIBE_Object_MatchNameCallback        (name-match walk cb)
//   0x5b7cb0  VIBE_Object_FindByName               (walk for name match)
//   0x4b0ee8  VIBE_Object_IsNearDoorAlt            (door-proximity, transport)
//   0x4b0f78  VIBE_Object_GetTypeMessageId         (type->message-id classify)
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// SceneNode3 — flat dword/byte view of the "d3:SpawnObject" node used by the
// transform/find family (a1[19], a1[27], a1[124], +528..). Named accessors at
// the recovered byte offsets; the raw dword[] keeps the 1:1 index access.
// (Only the touched fields are named; the block is 0x21C bytes.)
// ===========================================================================
struct SceneNode3 {
    u8 raw[0x21C];   // 540 bytes

    // --- byte/word helpers (faithful raw-offset access) ---
    float& f(int byteOff) { return *reinterpret_cast<float*>(raw + byteOff); }
    i32&   d(int byteOff) { return *reinterpret_cast<i32*>(raw + byteOff); }
    u8&    b(int byteOff) { return raw[byteOff]; }

    // Named offsets (documented):
    //   +0x00 (0)    name[0]  — first char; 33='!' prefix, 114='r' room
    //   +0x4C (76)   pos[3]   (a1[19..21])
    //   +0x6C (108)  scale[3] (a1[27..29])
    //   +0x78 (120)  pivot[3] (a1[30..32])
    //   +0x84 (132)  euler[3] (world-translation angles)
    //   +0x18C(396)  matrix base (a1+396) — VIBE_Math_MatrixFromEuler output
    //   +0x1EC(492)  drawData (a1[123])
    //   +0x1F0(496)  selfLink (a1[124]) — saved/cleared during a find walk
    //   +0x1FC(508)  firstChild (a1[127])
    //   +0x210(528)  flags528 — bit0=stop, bit2=dirty
    //   +0x212(530)  flags530 — bit7 cleared by MarkDirtyFlag(arg)
    //   +0x213(531)  flags531 — bit0 cleared by MarkDirtyFlag
    //   +0x215(533)  nodeType
    void reset() { for (auto& x : raw) x = 0; }
    SceneNode3() { reset(); }
};

// ---------------------------------------------------------------------------
// Mockable hooks for the unreconstructed leaves the originals call.
// nullptr => inert (the node-field arithmetic still runs faithfully). Tests
// install captors to assert ordering/args.
// ---------------------------------------------------------------------------
struct ObjLife3Hooks {
    // VIBE_SceneGraph_WalkAndInvoke(off_649D64, node, MarkDirtyFlag, mask, arg)
    // — recurse the subtree marking nodes dirty. We model the side effect the
    // transform setters depend on (MarkDirtyFlag on the root node) directly and
    // notify this hook for ordering assertions. Returns walk result (1).
    void (*walkMarkDirty)(SceneNode3* node, u16 mask, u8 arg) = nullptr;
    // VIBE_SceneGraph_TraverseTree(off_649D64, node, ResetCasterTransforms, 192)
    void (*traverseShadowReset)(SceneNode3* node, u16 mask) = nullptr;
    // VIBE_Math_MatrixFromEuler(angles, node+396) — fill the node's 3x3/4x4.
    void (*matrixFromEuler)(const float* angles, SceneNode3* node) = nullptr;
    // VIBE_Object_DetachAndRelease(node) — KillObject path.
    void (*detachAndRelease)(SceneNode3* node) = nullptr;
    // VIBE_Object_RebindParentMesh(node, prototype) — ReplaceObject path.
    void (*rebindParentMesh)(SceneNode3* node, int prototype) = nullptr;
    // VIBE_Sound3d_SetListenerOrientation(...) — MoveObject path.
    void (*sound3dMove)(SceneNode3* node, float x, float y, float z,
                        float wx, float wy, float wz, int extra) = nullptr;
    // VIBE_Rain_Create(node, a, b) / VIBE_Rain_Destroy(node).
    void* (*rainCreate)(SceneNode3* node, int a, int b) = nullptr;
    void  (*rainDestroy)(SceneNode3* node) = nullptr;
    // VIBE_Script_ReportError(msg) — the "Illegal object" diagnostic.
    void (*reportError)(const char* msg) = nullptr;
    // VIBE_Object_FindByHandle subtree string compare callbacks need a walker;
    // the find drivers below run a faithful recursive walk in-process (no hook),
    // so no walker hook is needed here.
};
void ObjLife3SetHooks(const ObjLife3Hooks& hooks);
void ObjLife3ResetHooks();

// ---------------------------------------------------------------------------
// Transform setters — see .cpp for faithful control flow.
// ---------------------------------------------------------------------------
// 0x5af38c — al = node@eax, &pos@edx. Marks dirty, shadow-reset, writes +76.
int  ObjectSetPosition(SceneNode3* node, const float pos[3]);
// 0x5af3ec — thunk packing (x,y,z) into a local vector.
int  ObjectSetPositionXYZ(SceneNode3* node, float x, float y, float z);
// 0x5af418 — writes +108 scale (a1[27..29]).
int  ObjectSetScaleVector(SceneNode3* node, const float scale[3]);
// 0x5af464 — thunk.
int  ObjectSetScaleVectorXYZ(SceneNode3* node, float x, float y, float z);
// 0x5af490 — writes +120 pivot (a1[30..32]).
int  ObjectSetPivotVector(SceneNode3* node, const float pivot[3]);
// 0x5af4e0 — thunk.
int  ObjectSetPivotVectorXYZ(SceneNode3* node, float x, float y, float z);
// 0x5af50c — writes +132 euler, builds matrix; nodeType==3 negates angles.
char ObjectSetWorldTranslation(SceneNode3* node, const float angles[3]);
// 0x5af5cc — thunk.
char ObjectSetWorldTranslationXYZ(SceneNode3* node, float x, float y, float z);
// 0x5af2e4 — al = isCurrent@al. Dirty the "current" node + sky/floor globals.
char ObjectInvalidateCurrent(u8 markChild);

// The "current object" global the transform setters compare against
// (gilde.exe dword_13FCD1C). Set to a node to make SetPosition route through
// InvalidateCurrent for that node.
extern SceneNode3* g_objCurrent;

// ---------------------------------------------------------------------------
// Command-layer mutators. The command-arg form takes a handle CELL (a1 points
// at the handle slot; *a1 is the node). We pass the node directly + a `valid`
// flag mirroring `*a1 != 0`.
// ---------------------------------------------------------------------------
// 0x43e724 — KillObject: if node, detachAndRelease; else reportError. ret 1.
int ObjectKillObject(SceneNode3* node);
// 0x43e758 — SetPos: node[19]=*x, node[20]=*z(ebx=y? see .cpp), node[21]=*y,
//            then SetPosition(node, &node[19]). ret 0.
int ObjectSetPos(SceneNode3* node, float x, float y, float z);
// 0x43e804 — MoveObject: 3d-sound listener move. ret 0.
int ObjectMoveObject(SceneNode3* node, int x, int y, int z, int extra);
// 0x43ea48 — ReplaceObject: rebindParentMesh(node, prototype). ret 1.
int ObjectReplaceObject(SceneNode3* node, int prototype);
// 0x43f844 — rain create thunk. returns rainCreate result.
void* ObjectCmdSetObjectStateThunk(SceneNode3* node, int a, int b);
// 0x43f84c — rain destroy thunk. ret 0.
int  ObjectCmdResetObjectThunk(SceneNode3* node);

// ---------------------------------------------------------------------------
// 0x5b3698 — ToggleHiddenState. nodeType 5<->6 gated on name[0]=='r' and arg.
//   arg `dword_62EB38` (the global frame/tick) is supplied as `frameStamp`.
//   Returns 1 always.
// ---------------------------------------------------------------------------
char ObjectToggleHiddenState(SceneNode3* node, char enable, i32 frameStamp);

// ---------------------------------------------------------------------------
// 0x583a70 — FindObjectById. Linear scan of g_objects (stride 67 BYTES here:
//   *(WORD)(base+v) marker, *(DWORD)(base+v+2) id). Returns base+v or 0.
//   To stay 1:1 with the raw stride-67 byte layout without redefining the
//   shared g_objects, the caller supplies the table base + entry count.
//   Each entry: word marker @+0, dword id @+2. Returns index of match or -1.
// ---------------------------------------------------------------------------
int ObjectFindObjectByIdIndex(const u8* table, int entryCount, int stride,
                              i32 id);

// ---------------------------------------------------------------------------
// Find-by-handle / find-by-name. The walk callbacks compare a node's name
// against a query and record the first match into outFound. The original packs
// {queryStr, queryNode, outFound} into a 3-dword struct; we model it as
// FindCtx and run the same comparison logic.
// ---------------------------------------------------------------------------
struct FindCtx {
    const char*  queryStr;   // a2[0]
    SceneNode3*  queryNode;  // a2[1]
    SceneNode3*  found;      // a2[2] (output)
};
// 0x5b7b7c — case-SENSITIVE handle match callback. Returns "keep walking"
//   (node != found). Faithful to the '!' prefix skip + queryNode fast path.
bool ObjectMatchHandleCallback(SceneNode3* node, FindCtx* ctx);
// 0x5b7c48 — case-INSENSITIVE name match callback.
bool ObjectMatchNameCallback(SceneNode3* node, FindCtx* ctx);

// ---------------------------------------------------------------------------
// 0x4b0ee8 — IsNearDoorAlt (transport door proximity). Pure-geometry core.
//   Mirrors object_lifecycle2's ObjectIsNearDoorCore but with the transport
//   tolerances (650 if door-dummy found, 3250 fallback).
// ---------------------------------------------------------------------------
struct DoorProximityAltInputs {
    bool   hasActor;          // v3 = *(a1+59) != 0
    bool   targetHasAnchor;   // v4 = *(a2+97) != 0
    bool   doorDummyFound;    // FindByHandle("dummy_TUER") != 0
    float  doorPos[3];        // *(v3+52)+76 — door anchor pos
    float  testPos[3];        // bone-chain point (door dummy or target anchor)
    i32    actorOwnerId;      // *(v3+44)
    i32    targetOwnerId;     // *(a2+1)
};
bool ObjectIsNearDoorAltCore(const DoorProximityAltInputs& in);

// ---------------------------------------------------------------------------
// 0x4b0f78 — GetTypeMessageId. Maps an object's type byte (*(node+380)) to one
//   or two message ids written into out[0]/out[1], returning the count (0/1/2),
//   then optionally appends one more from the combat object-def. Faithful to
//   the big switch on the type byte. `typeByte` = **(node+380). The "extra
//   value" appends (item/coin amounts >> 16) are passed in via the inputs.
// ---------------------------------------------------------------------------
struct TypeMessageInputs {
    int  typeByte;        // *(*(node+380)) ; -1 => no type ptr
    int  itemHighWord;    // *(v4+170)>>16 / *(v4+186)>>16 / *((int*)v4+43)>>16
    int  buildingKind;    // *(589*type + buildingTypes) for type==4
    int  combatDefValue;  // *(WORD)FindObjectDef(node) ; -1 => no def
    bool hasCombatDef;    // FindObjectDef(node) && *(WORD)def != 0
};
// Writes message ids into out (room for 3). Returns the count.
int ObjectGetTypeMessageId(const TypeMessageInputs& in, i32 out[3]);

}  // namespace guild::sim
