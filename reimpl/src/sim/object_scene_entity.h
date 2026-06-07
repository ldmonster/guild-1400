#pragma once
// ===========================================================================
// object_scene_entity.{h,cpp} — VIBE_Entity_* / VIBE_Transform_* /
// VIBE_GameObject_* remaining deterministic leaves (Wave 30 P6).
// namespace guild::sim.
// ===========================================================================
// MODULE: the still-deferred *deterministic* leaves of the object-lifecycle /
// scene-graph / character subsystem that the object_lifecycle1..10, entity.cpp,
// person.cpp, render/scene_walk.cpp and util/transform.cpp batches did NOT cover.
// Every function here was confirmed deferred by (1) its entry address being
// absent from the src/ provenance done-set AND (2) its bare name being absent
// from any src/ definition (see the report for the SKIPPED already-done set).
//
// Group A — Entity record predicates (operate on g_persons, the 536-stride
//   person/entity table word_12CE910). These are the type-classification
//   siblings of VIBE_Person_IsValidActiveRecord (0x4f8e60, person.cpp):
//   * IsAnimalType        0x4f8e9c — kind byte (+2) in {5,6,7}        (animals)
//   * IsPersonType        0x4f8ee4 — kind byte (+2) <10 && !={6,7}    (people)
//   * IsCarriedType       0x4f8f2c — kind byte (+2) in {6,7}          (carried)
//   each gated by marker(+0)!=-1 && live-actor byte(+8)!=0.
//
// Group B — Entity selection / field leaves:
//   * IsNotInSelectionList 0x4f8e1c — true iff `id` is absent from the 4x17 (==68)
//     entry selection table dword_122DAE4 (the function scans groups of 8 entries,
//     stride 17 dwords/group; on the FIRST mismatching group it short-circuits to
//     "found" — see body comment). The selection table is a self-contained global
//     touched ONLY by this function in the binary; modeled here with a public
//     setter so tests can seed it.
//   * ScaleField148 0x5b839c — multiply a node's +148 float scalar by *scale.
//   * AnimationUpdate 0x4244f8 — stash an (x,y,w,h)-style rect into a record's
//     dword slots [9..12] (the gui entity-anim hook target, VIBE_Entity_*).
//
// Group C — Transform pivot:
//   * PointPivotToFrameSpace 0x5c8de8 — transform a point into a pivot frame's
//     space relative to another frame, reusing the REAL util leaf
//     util::PointThroughBoneChainPivot (0x5c8d0c).
//
// Group D — GameObject table teardown:
//   * FreeAllTables 0x583ab0 — free every building (Person_QueryBegin op6 ->
//     Building_FreeAndUnlink) then release the four global record tables
//     (g_persons / g_objects / scene-typedef / g_sceneNodes) — in the reimpl the
//     tables are static arrays, so "free" == zero them + drop the loaded guards.
//     The per-building unlink and the table-release are routed through inert hooks
//     (defined in the .cpp) so the leaf has no un-reconstructed dependency.
// ===========================================================================
#include "guild/common/types.h"

namespace guild::sim {

// --- Group A: entity type predicates (slot index into g_persons) -----------

// gilde.exe 0x4f8e9c — VIBE_Entity_IsAnimalType (__usercall, eax=ret, ax=idx).
bool EntityIsAnimalType(u16 idx);
// gilde.exe 0x4f8ee4 — VIBE_Entity_IsPersonType (__usercall, eax=ret, ax=idx).
bool EntityIsPersonType(u16 idx);
// gilde.exe 0x4f8f2c — VIBE_Entity_IsCarriedType (__usercall, eax=ret, ax=idx).
bool EntityIsCarriedType(u16 idx);

// --- Group B: selection list + node field leaves ---------------------------

// gilde.exe 0x4f8e1c — VIBE_Entity_IsNotInSelectionList (__usercall, eax=id).
// Returns 1 if `id` is not present in the selection table, 0 if it is.
int EntityIsNotInSelectionList(i32 id);

// Selection table accessors (own global; original dword_122DAE4, 68 entries).
constexpr int kSelectionTableSize = 68;
void   EntitySetSelectionEntry(int index, i32 id);  // index 0..67
i32    EntityGetSelectionEntry(int index);
void   EntityClearSelectionList();                  // zero all 68 entries

// gilde.exe 0x5b839c — VIBE_Entity_ScaleField148 (__usercall, eax=node, edx=*f).
// *(float*)(node+148) *= *scale;  returns 1. `node` is the raw record base.
char EntityScaleField148(void* node, const float* scale);

// gilde.exe 0x4244f8 — VIBE_Entity_AnimationUpdate (__userpurge).
// Register-named args (eax=a1, edx=a2, ecx=a3, ebx=a4, stack=rec). If rec != 0:
//   rec[9]=a1; rec[10]=a2; rec[11]=a1+a4; rec[12]=a3+a2. Returns a1+a4 (the
//   original returns `result` after `result += a4`). `rec` is a dword[].
int EntityAnimationUpdate(int a1, int a2, int a3, int a4, i32* rec);

// --- Group C: transform pivot ----------------------------------------------

// gilde.exe 0x5c8de8 — VIBE_Transform_PointPivotToFrameSpace.
//   if frame+528 sign bit set OR point==null: return PointThroughBoneChainPivot.
//   else: P = PointThroughBoneChainPivot(frame); P -= pivot[19..21]; P -=
//   pivot[30..32]; out = (3x3 cols pivot[99/103/107|100/104/108|101/105/109]) * P.
// Args mirror the original register map: frame=a1, pivot=a2, out=a3, in=a4 (the
// point). `frame`/`pivot`/`out` are float-array views of the mesh-frame blocks.
// Returns `out` (or, in the bone-chain fallthrough, that call's result).
float* TransformPointPivotToFrameSpace(float* frame, const float* pivot,
                                       float* out, const float* in);

// --- Group D: table teardown -----------------------------------------------

// Hooks for FreeAllTables' two un-reconstructed leaves. Defaults are inert
// (defined in the .cpp): the per-building unlink is a no-op, and the table
// release zeroes the static record arrays and drops the loaded guards.
struct ObjectSceneEntityHooks {
    // VIBE_Building_FreeAndUnlink 0x586d6c — called once per live building.
    void (*buildingFreeAndUnlink)(void* building) = nullptr;
    // Release the four global tables (the original frees the heap blocks; the
    // reimpl zeroes the static arrays). Called once after the building sweep.
    void (*releaseTables)() = nullptr;
};
void SetObjectSceneEntityHooks(ObjectSceneEntityHooks* hooks);
ObjectSceneEntityHooks* ObjectSceneEntityHooksPtr();

// gilde.exe 0x583ab0 — VIBE_GameObject_FreeAllTables (__usercall).
// Returns the number of buildings unlinked (the original returns the last freed
// table pointer; in the reimpl the meaningful observable is the unlink count).
int GameObjectFreeAllTables();

} // namespace guild::sim
