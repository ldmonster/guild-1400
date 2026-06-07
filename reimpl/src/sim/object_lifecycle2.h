#pragma once
// ===========================================================================
// object_lifecycle2.{h,cpp} — VIBE_Object_* scene-object lifecycle / transform /
// fill / occupant record mutators (namespace guild::sim).
// ===========================================================================
// MODULE: the remaining VIBE_Object_* lifecycle/transform/fill/occupant leaves
// of gilde.exe that operate on (a) the heap-allocated scene-GRAPH object node
// (the 0x21C-byte "d3:SpawnObject" block) and (b) the gameplay RECORD arrays
// already recovered elsewhere:
//   * g_objects      (gilde.exe dword_13CE298, stride 169, 256)  — sim/entity.h
//   * g_buildingTypes(gilde.exe dword_13CE294, stride 589)       — sim/building.h
//   * g_sceneTypes   (gilde.exe dword_13CE27C, stride 65)        — building_production.h
//   * g_sceneTypeRemap(gilde.exe byte_13CE862, [731])            — building_production.h
//
// The scene-graph OBJECT node (the SetParent/Link/transform family operates on
// this) is one opaque ~0x21C-byte block addressed by raw byte offsets. The
// render module (src/render/scene_walk.h) models the *link/walk* subset of this
// node as guild::render::SceneNode. This module needs a DIFFERENT, larger view
// of the SAME block (transform vectors at +132/+76, flag bytes at +528..+535,
// the tile-flag byte at +7280/+7281, the particle-emitter block at +488). To
// stay 1:1 without colliding with the render struct, we model it here as
// SceneObject with NAMED members at the recovered byte offsets and keep the
// exact bit/word semantics. Offsets are documented on every field.
//
// Functions that bottom out in unreconstructed render / sound / floor / script
// leaves (VIBE_Object_SelectTextureSet, VIBE_Render_SetMipFilterLevel,
// VIBE_Floor_*, VIBE_Sound3d_*, VIBE_Script_ReportError, VIBE_Universe_*) are
// routed through a small set of mockable HOOKS so the record/flag logic is
// exact and independently testable.
//
// Translated functions (addresses are gilde.exe / imagebase 0x400000):
//   0x583a2c  VIBE_Object_IsBuildingType            (scene type-def kind classify)
//   0x586508  VIBE_Object_CollectMatchingProts      (build matching-prot spawn list)
//   0x4ffee8  VIBE_Object_ResetSpawnTables          (init the 731/72 spawn tables)
//   0x5a8140  VIBE_Object_RebuildModelByOwner       (rebuild models for an owner)
//   0x5a8184  VIBE_Object_ClearVisualFlag           (clear node+531 bit2)
//   0x43f434  VIBE_Object_CmdSetActiveHandle        (set active-object global)
//   0x43fc8c  VIBE_Object_SetBlocked                (node+530 bit4 from arg bit0)
//   0x43fcd8  VIBE_Object_SetTransient              (node+530 bits2-3 from arg bits0-1)
//   0x43e79c  VIBE_Object_SetAngle                  (node euler @+132 deg->rad + dirty)
//   0x500134  VIBE_Object_UnlinkFromChain           (unlink node from +512 chain)
//   0x500174  VIBE_Object_InitParticleEmitters      (seed emitter lifetimes)
//   0x506388  VIBE_Object_HideFoliageDecor          (foliage texture-set select)
//   0x506430  VIBE_Object_HideFoliageByState        (foliage texture-set, type-driven)
//   0x4b0e64  VIBE_Object_IsNearDoor                (door-proximity test)
//   0x4b0ee8  VIBE_Object_IsNearDoorAlt             (door-proximity test, transport)
#include <cstdint>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ===========================================================================
// SceneObject — the heap "d3:SpawnObject" node (gilde.exe alloc size 0x21C).
// Named members at the recovered byte offsets the VIBE_Object_* family reads.
// (The render-link subset of the same block is guild::render::SceneNode; this
// is the transform/flag/fill superset view. Only the touched fields are named;
// padding keeps the offsets faithful.)
// ===========================================================================
struct SceneObject {
    char name[64];          // +0x00 (0)   name string (StrNCopyPad'd at spawn)
    float pos[3];           // +0x4C (76)  world position (x,y,z) — bone-chain src
    float angle[3];         // +0x84 (132) euler angle (rad) — SetAngle / world tr.
    float pivot;            // +0x90 (144) (unused here; keeps spacing)
    void* emitterBlock;     // +0x1E8 (488) particle-emitter block base (7 * 56 B)
    void* drawData;         // +0x1EC (492) draw-data / mesh ptr (+460 alias model)
    void* model;            // +0x1CC (460) model/mesh root (used by foliage select)
    void* prevList;         // +0x1F0 (496) intrusive list prev (UnlinkFromChain)
    void* nextList;         // +0x1F4 (500) intrusive list next
    SceneObject* parent;    // +0x1F8 (504) parent  (UnlinkFromChain walks +504)
    SceneObject* firstChild;// +0x1FC (508) first child
    i32  ownerKey;          // +0x200 (512) owner/model key (RebuildModelByOwner +512)
    u8   flags528;          // +0x210 (528) bit2 = transform dirty
    u8   flags529;          // +0x211 (529)
    u8   flags530;          // +0x212 (530) bit4 = blocked, bits2-3 = transient
    u8   flags531;          // +0x213 (531) bit2 = visual flag (ClearVisualFlag)
    u8   nodeType;          // +0x215 (533) node-type byte (0..8)
    u8   nodeTypeShadow;    // +0x216 (534) copy of nodeType
    u8   visualState;       // +0x217 (535) visual/upgrade state (2/3/4 = hidden)
    u8   tileFlag;          // +0x1C70 (7280) tile/visual-state flag (high-level)
    u8   tileNibble;        // +0x1C71 (7281) low-nibble floor flag

    SceneObject() { reset(); }
    void reset() {
        for (auto& c : name) c = 0;
        pos[0] = pos[1] = pos[2] = 0.f;
        angle[0] = angle[1] = angle[2] = 0.f;
        pivot = 0.f;
        emitterBlock = drawData = model = prevList = nextList = nullptr;
        parent = firstChild = nullptr;
        ownerKey = 0;
        flags528 = flags529 = flags530 = flags531 = 0;
        nodeType = nodeTypeShadow = visualState = 0;
        tileFlag = tileNibble = 0;
    }
};

// ---------------------------------------------------------------------------
// Constants recovered from the originals.
// ---------------------------------------------------------------------------
// 0x583a2c — type-def kind values that classify as a "building" type.
constexpr int kSpawnTableProtCount   = 731;   // CollectMatchingProts / ResetSpawnTables
constexpr int kSpawnTableSlotCount   = 72;    // ResetSpawnTables second table
constexpr int kSpawnRemapHiddenProt  = 72;    // byte_13CE862 sentinel skipped (== 72)

// 0x43e79c / SetAngle — degrees->radians scale (PI * (1/180)).
constexpr float kPi          = 3.1415927410125732f;       // flt_617380
constexpr float kDegPerRadInv= 0.0055555556900799274f;    // flt_617384 (1/180)

// 0x500174 — particle-emitter seed factors are module constants in the .cpp
// (flt_620BF4 = 0.05, flt_620BF8 = 0.8, flt_620BFC = 0.5).

// ---------------------------------------------------------------------------
// Mockable hooks for the unreconstructed leaves the originals call. nullptr =>
// no-op (the record/flag logic still runs faithfully). Tests install captors.
// ---------------------------------------------------------------------------
struct ObjLifeHooks {
    // VIBE_Object_BuildModelName(model, &g_objects[i], mode=2) — rebuilds a
    // building's model. RebuildModelByOwner calls it per matching object slot.
    void (*buildModelName)(SceneObject* model, ObjectRec* obj, u8 mode) = nullptr;
    // VIBE_Object_SelectTextureSet(obj, mesh, 1, set, arg) — foliage hide.
    void (*selectTextureSet)(SceneObject* obj, int set) = nullptr;
    // VIBE_Script_ReportError — the "Illegal object" diagnostic path.
    void (*reportError)(const char* msg) = nullptr;
};
void ObjLifeSetHooks(const ObjLifeHooks& hooks);
void ObjLifeResetHooks();

// ---------------------------------------------------------------------------
// 0x583a2c — VIBE_Object_IsBuildingType  (__usercall eax = fn(type@ax)).
//   v1 = g_sceneTypes[type].kind; returns true iff v1 in {1,3,4,11,26,27,28}.
// ---------------------------------------------------------------------------
bool ObjectIsBuildingType(i16 type);

// ---------------------------------------------------------------------------
// 0x586508 — VIBE_Object_CollectMatchingProts (__usercall eax = fn(building@eax)).
//   For each prot i in 0..730 with remap != 72: if remap >= building.type and
//   g_buildingTypes[remap].kind == g_buildingTypes[building.type].kind, record i
//   into g_objSpawnList[count++]. Stores count in g_objSpawnCount. Returns 1 if
//   the scene type table is absent (g_sceneTypesLoaded == false), else 0.
//   `buildingTypeByte` is the building's +0 type byte (*a1).
//
//   `remap` is the gilde.exe byte_13CE862 per-prot building-type remap, a
//   731-entry signed-byte table. The reimpl's shared g_sceneTypeRemap is sized
//   for the 0..255 market-price access pattern (truncated); to stay faithful to
//   the full 0..730 scan WITHOUT redefining that global or reading it OOB, this
//   function takes the remap table as a caller-supplied pointer (length 731).
// ---------------------------------------------------------------------------
int ObjectCollectMatchingProts(u8 buildingTypeByte, const i8* remap);

// The spawn-list output (gilde.exe word_13CE29C[731] + count word_13CE860).
extern u16 g_objSpawnList[kSpawnTableProtCount];
extern u16 g_objSpawnCount;

// ---------------------------------------------------------------------------
// 0x4ffee8 — VIBE_Object_ResetSpawnTables.
//   Fills the per-prot owner byte table (731 entries) and the per-slot owner
//   byte table (72 entries) with -1. Returns 0.
//   Modeled as g_objSpawnOwner[731] / g_objSlotOwner[72] (byte +3 of each
//   dword stride in the original; here a flat byte array).
// ---------------------------------------------------------------------------
char ObjectResetSpawnTables();
extern i8 g_objSpawnOwner[kSpawnTableProtCount];
extern i8 g_objSlotOwner[kSpawnTableSlotCount];

// ---------------------------------------------------------------------------
// 0x5a8140 — VIBE_Object_RebuildModelByOwner (__usercall al = fn(model@eax)).
//   Walks g_objects (stride 169, bound 43264): for each alive slot whose id
//   (+1) equals model.ownerKey (+512), invokes buildModelName(model, slot, 2).
//   Returns 1.
// ---------------------------------------------------------------------------
char ObjectRebuildModelByOwner(SceneObject* model);

// ---------------------------------------------------------------------------
// 0x5a8184 — VIBE_Object_ClearVisualFlag (__usercall al = fn(node@eax)).
//   node.flags531 &= ~4; returns 1.
// ---------------------------------------------------------------------------
char ObjectClearVisualFlag(SceneObject* node);

// ---------------------------------------------------------------------------
// 0x43f434 — VIBE_Object_CmdSetActiveHandle (__usercall eax = fn(&handle@eax)).
//   dword_649D7D = *a1; returns 1.  (Sets the active-object global.)
// ---------------------------------------------------------------------------
int ObjectCmdSetActiveHandle(SceneObject* handle);
extern SceneObject* g_objActiveHandle;   // gilde.exe dword_649D7D

// ---------------------------------------------------------------------------
// 0x43fc8c — VIBE_Object_SetBlocked (__usercall eax = fn(&node@eax, &flag@edx)).
//   If *a1 != 0: node.flags530 = (node.flags530 & 0xEF) | (16 * (*flag & 1));
//   returns 1. Else reportError("SetBlocked: Illegal object...") and returns 0.
// ---------------------------------------------------------------------------
int ObjectSetBlocked(SceneObject* node, u8 flag);

// ---------------------------------------------------------------------------
// 0x43fcd8 — VIBE_Object_SetTransient (__usercall eax = fn(&node@eax, &flag@edx)).
//   If *a1 != 0: node.flags530 = (node.flags530 & 0xF3) | (4 * (*flag & 3));
//   returns 1. Else reportError("SetTransient: Illegal object...") and 0.
// ---------------------------------------------------------------------------
int ObjectSetTransient(SceneObject* node, u8 flag);

// ---------------------------------------------------------------------------
// 0x43e79c — VIBE_Object_SetAngle (__usercall eax = fn(&node@eax, x@edx,z@ecx,y@ebx)).
//   If *a1 != 0: node.angle[0] = *x * PI * (1/180); angle[1] = *y * PI/180;
//   angle[2] = *z * PI/180 (note the original's z/y arg swap into [1]/[2]);
//   sets node.flags528 |= 4. Else reportError. Returns 0.
//   The original takes float* args (script value cells); we take floats and the
//   same edx=x, ecx=z, ebx=y register mapping.
// ---------------------------------------------------------------------------
int ObjectSetAngle(SceneObject* node, float x, float z, float y);

// ---------------------------------------------------------------------------
// 0x500134 — VIBE_Object_UnlinkFromChain (__usercall eax = fn(node@eax)).
//   v2 = node.ownerKey(+512); result = node.parent(+504). If v2 == 0, walk the
//   parent chain copying each parent's +512 into node.+512 until node.+512 != 0
//   or the chain ends. Returns the last parent visited.
//   (Faithful to the pointer-chasing loop; +512 read as i32 ownerKey, +504 as
//   parent.)
// ---------------------------------------------------------------------------
SceneObject* ObjectUnlinkFromChain(SceneObject* node);

// ---------------------------------------------------------------------------
// 0x500174 — VIBE_Object_InitParticleEmitters (__usercall eax = fn(node@eax)).
//   Only for nodeType==6. For each of 7 emitters (stride 56 B in emitterBlock)
//   with |life| (dword[8]) == 0: seed life = (e[6]+e[7]) * F1, e[7]*=F2 (@+28),
//   e[9]*=F3 (@+36). Then for the node's own emitter (@+92..+148): if |node+100|
//   == 0, node+100 = (node+92 + node+96)*F1; node+96 *= F2; node+148 *= F3.
//   Modeled with an Emitter struct + the node's own emitter fields. Returns node.
// ---------------------------------------------------------------------------
struct Emitter {            // 56-byte emitter record (14 floats)
    float f[14];
};
struct NodeEmitter {        // the node's own emitter fields (+92/+96/+100/+148)
    float spawnA;   // +92  (node[23])
    float spawnB;   // +96  (node[24])
    float life;     // +100 (node[25])
    float decay;    // +148 (node[37])
};
SceneObject* ObjectInitParticleEmitters(SceneObject* node, Emitter emitters[7],
                                        NodeEmitter* own);

// ---------------------------------------------------------------------------
// 0x506388 — VIBE_Object_HideFoliageDecor (__usercall al = fn(node@eax)).
//   If node.name starts with "pfl_" OR "vg_" OR "!vg_", calls
//   selectTextureSet(node, byte_634484). Returns the strncmp result (faithful:
//   the original returns v4, the last strncmp's value, which is 0 on a match).
//   We expose it as: returns 1 if a hide happened, else the non-zero strncmp.
// ---------------------------------------------------------------------------
char ObjectHideFoliageDecor(SceneObject* node, int textureSet);

// ---------------------------------------------------------------------------
// 0x506430 — VIBE_Object_HideFoliageByState (__usercall al = fn(node@eax, &bldgType@edx)).
//   v5 = g_buildingTypes[*bldgType].security - 1; if kind==2, v5 = security.
//   If node.visualState in {2,3,4}: return 1 (already hidden). Else if name does
//   NOT start with "pfl_"/"vg_"/"!vg_": for i in 0..v5, selectTextureSet(node,i).
//   Returns 1. (The original's selectTextureSet 5-arg call is reduced to the
//   (node,set) hook; the per-i loop bound v5 is preserved.)
// ---------------------------------------------------------------------------
char ObjectHideFoliageByState(SceneObject* node, u8 buildingTypeByte);

// ---------------------------------------------------------------------------
// 0x4b0e64 — VIBE_Object_IsNearDoor (door-proximity test).
//   Pure geometry: actor (node) at +388 carries a door anchor; tests whether the
//   target's door dummy (or the target itself) is within 750 units of the door,
//   gated on the door's "open" flag (+74). Falls back to an owner-id compare.
//   Because the real impl chains through FindByHandle / PointThroughBoneChain /
//   VectorWithinTolerance (transform leaves owned elsewhere), this is exposed as
//   a TESTABLE pure-math core taking the already-resolved inputs. See the .cpp
//   for the faithful control flow and the documented input mapping.
//   Returns true if "near door".
struct DoorProximityInputs {
    bool   hasActor;          // node+388 (v3) != 0
    bool   targetHasAnchor;   // *(target+97) != 0
    bool   doorDummyFound;    // FindByHandle("dummy_TUER") != 0
    float  doorPos[3];        // door anchor world pos (v3[13]+76)
    float  testPos[3];        // bone-chain point of the door dummy/target
    bool   doorOpenFlag;      // v3[74] (0 => "open"/passable -> near)
    i32    actorOwnerId;      // v3[11]
    i32    targetOwnerId;     // *(target+1)
    float  tolerance;         // 750.0 (default)
};
bool ObjectIsNearDoorCore(const DoorProximityInputs& in);

}  // namespace guild::sim
