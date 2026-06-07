// ===========================================================================
// object_scene_entity.cpp — see object_scene_entity.h for the module overview.
// Faithful 1:1 reconstructions of the still-deferred deterministic leaves of the
// VIBE_Entity_* / VIBE_Transform_* / VIBE_GameObject_* families.
// ===========================================================================
#include "sim/object_scene_entity.h"

#include "sim/entity.h"        // g_persons, g_objects, g_sceneNodes, guards,
                               // PersonQueryBegin/IterNext, ResetEntityArrays
#include "sim/person.h"        // PersonGetByte, kPfKind / kPfIsPlayer
#include "sim/types.h"         // Person, kPfKind, kPfIsPlayer
#include "util/transform.h"    // PointThroughBoneChainPivot (0x5c8d0c)

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Group A — entity type predicates.
//
// VIBE_Entity_IsAnimalType 0x4f8e9c:
//   v1 = 536 * idx;                                  // record byte base
//   if (word_12CE910[v1/2] != -1)                    // marker (+0) != free
//     if (byte_12CE918[v1])                           // live-actor (+8) != 0
//       v2 = byte_12CE912[v1];                         // kind byte (+2)
//       if (v2 == 6 || v2 == 7 || v2 == 5) return 1;
//   return 0;
// In the reimpl g_persons[idx] folds the 536-stride record: .marker == +0,
// kPfIsPlayer == +8, kPfKind == +2. The three predicates differ only in the
// kind-set they accept.
// ===========================================================================
namespace {
inline bool EntityActiveActor(u16 idx) {
    const Person& p = g_persons[idx];
    return p.marker != -1 && PersonGetByte(&p, kPfIsPlayer) != 0;
}
} // namespace

bool EntityIsAnimalType(u16 idx) {
    if (EntityActiveActor(idx)) {
        u8 kind = PersonGetByte(&g_persons[idx], kPfKind);  // dh: signed char
        if (kind == 6 || kind == 7 || kind == 5)
            return true;
    }
    return false;
}

bool EntityIsPersonType(u16 idx) {
    if (EntityActiveActor(idx)) {
        // v2 = byte (read as signed char dh); v2 < 10 && v2 != 6 && v2 != 7.
        signed char kind = static_cast<signed char>(PersonGetByte(&g_persons[idx], kPfKind));
        if (kind < 10 && kind != 6 && kind != 7)
            return true;
    }
    return false;
}

bool EntityIsCarriedType(u16 idx) {
    if (EntityActiveActor(idx)) {
        u8 kind = PersonGetByte(&g_persons[idx], kPfKind);
        if (kind == 6 || kind == 7)
            return true;
    }
    return false;
}

// ===========================================================================
// Group B — selection list + node field leaves.
//
// VIBE_Entity_IsNotInSelectionList 0x4f8e1c:
//   for (i = 0; i < 68; i += 17) {            // four 17-dword groups
//     v3 = i * 4;                              // byte offset of entry [i]
//     v4 = 0;
//     if (a1 == dword_122DAE4[i]) return 0;    // checks entry [i]
//     while (1) {
//       ++v4; v3 += 8;                          // advance 2 dwords per step (!)
//       if (v4 >= 8) break;
//       if (a1 == *(int*)((char*)dword_122DAE4 + v3)) return 0;
//     }
//   }
//   return 1;
// VERBATIM QUIRK: the inner stride is 8 BYTES (== 2 dwords), so each group of
// 17 dwords only probes the EVEN offsets {i, i+2, i+4, ..., i+14} (8 entries).
// We reproduce the exact index pattern; the odd entries are never compared.
// ===========================================================================
namespace {
i32 g_selectionTable[kSelectionTableSize] = {0};  // dword_122DAE4 (own global)
} // namespace

void EntitySetSelectionEntry(int index, i32 id) {
    if (index >= 0 && index < kSelectionTableSize)
        g_selectionTable[index] = id;
}
i32 EntityGetSelectionEntry(int index) {
    if (index >= 0 && index < kSelectionTableSize)
        return g_selectionTable[index];
    return 0;
}
void EntityClearSelectionList() {
    std::memset(g_selectionTable, 0, sizeof(g_selectionTable));
}

int EntityIsNotInSelectionList(i32 id) {
    for (int i = 0; i < 68; i += 17) {
        int v3 = i * 4;                    // byte offset (== dword index i)
        int v4 = 0;
        if (id == g_selectionTable[i])
            return 0;
        while (true) {
            ++v4;
            v3 += 8;                        // +2 dwords
            if (v4 >= 8)
                break;
            int didx = v3 / 4;              // even index i+2, i+4, ... i+14
            if (didx < kSelectionTableSize && id == g_selectionTable[didx])
                return 0;
        }
    }
    return 1;
}

// VIBE_Entity_ScaleField148 0x5b839c:
//   *(float*)(a1 + 148) = *a2 * *(float*)(a1 + 148);  return 1;
char EntityScaleField148(void* node, const float* scale) {
    float* field = reinterpret_cast<float*>(static_cast<char*>(node) + 148);
    *field = *scale * *field;
    return 1;
}

// VIBE_Entity_AnimationUpdate 0x4244f8: stash a rect into rec[9..12].
int EntityAnimationUpdate(int a1, int a2, int a3, int a4, i32* rec) {
    int result = a1;
    if (rec) {
        rec[9]  = a1;
        rec[10] = a2;
        result += a4;
        rec[11] = result;
        rec[12] = a3 + a2;
    }
    return result;
}

// ===========================================================================
// Group C — transform pivot.
//
// VIBE_Transform_PointPivotToFrameSpace 0x5c8de8:
//   if (*(char*)(a1 + 528) < 0 || !a2)            // a1[132] high bit, or no pivot
//     return PointThroughBoneChainPivot(a1, a4, a3);
//   PointThroughBoneChainPivot(a1, a4, &local);   // local = world-space point
//   local -= a2[19..21];                            // pivot translation A
//   local -= a2[30..32];                            // pivot translation B
//   out[0] = local . a2[99/103/107];                // 3x3 column read
//   out[1] = local . a2[100/104/108];
//   out[2] = local . a2[101/105/109];
//   return out;
// Args: a1=frame@eax, a2=pivot@edx, a3=out@ecx, a4=in(point)@ebx. The local
// double accumulation matches the original's x87 spill (v6/v7 = st doubles).
// ===========================================================================
float* TransformPointPivotToFrameSpace(float* frame, const float* pivot,
                                       float* out, const float* in) {
    // a1[132] == byte at +528; the original tests the SIGN of that byte.
    const signed char flag528 = reinterpret_cast<const signed char*>(frame)[528];
    if (flag528 < 0 || !pivot) {
        // Fallthrough: PointThroughBoneChainPivot(a1=frame, a4=in, a3=out).
        return util::PointThroughBoneChainPivot(frame, in, out);
    }
    // local = bone-chain pivot of the point, in world space.
    float local[3];
    util::PointThroughBoneChainPivot(frame, in, local);
    local[0] -= pivot[19];
    local[1] -= pivot[20];
    local[2] -= pivot[21];
    local[0] -= pivot[30];
    local[1] -= pivot[31];
    local[2] -= pivot[32];
    // out = (3x3 columns pivot[99/103/107 | 100/104/108 | 101/105/109]) * local.
    // The original accumulates v6/v7 (st doubles) for [1]/[2] before storing [0].
    double v6 = (double)local[0] * pivot[100] + (double)local[1] * pivot[104]
              + (double)local[2] * pivot[108];
    double v7 = (double)local[0] * pivot[101] + (double)local[1] * pivot[105]
              + (double)local[2] * pivot[109];
    out[0] = (float)((double)local[0] * pivot[99] + (double)local[1] * pivot[103]
              + (double)local[2] * pivot[107]);
    out[1] = (float)v6;
    out[2] = (float)v7;
    return out;
}

// ===========================================================================
// Group D — table teardown.
//
// VIBE_GameObject_FreeAllTables 0x583ab0:
//   for (i = Person_QueryBegin(spec, 1, 6); i; i = Person_IterNext())
//     Building_FreeAndUnlink(i, ...);              // unlink every building
//   if (dword_13CE294) { FreeDebug(dword_13CE294); dword_13CE294 = 0; }  // person table
//   if (dword_13CE298) { FreeDebug(dword_13CE298); dword_13CE298 = 0; }  // object table
//   if (dword_13CE27C) { FreeDebug(dword_13CE27C); dword_13CE27C = 0; }  // scene typedef
//   if (dword_13CE290) { FreeDebug(dword_13CE290); dword_13CE290 = 0; }  // scene nodes
//   return <last freed ptr>;
// The reimpl tables are static arrays (entity.cpp), so "free" is modeled as
// zeroing them + dropping the loaded guards (via releaseTables hook). The
// per-building unlink is routed through buildingFreeAndUnlink. We return the
// number of buildings swept (the meaningful observable).
// ===========================================================================
namespace {

void DefaultReleaseTables() {
    // Mirror the original's "all four bases now null" end state. ResetEntityArrays
    // clears the live arrays + loaded guards exactly as a fresh (unloaded) game.
    ResetEntityArrays();
}

ObjectSceneEntityHooks  g_defaultHooks = { /*buildingFreeAndUnlink=*/nullptr,
                                           /*releaseTables=*/&DefaultReleaseTables };
ObjectSceneEntityHooks* g_hooks = &g_defaultHooks;

} // namespace

void SetObjectSceneEntityHooks(ObjectSceneEntityHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
ObjectSceneEntityHooks* ObjectSceneEntityHooksPtr() { return g_hooks; }

int GameObjectFreeAllTables() {
    int swept = 0;
    // Person_QueryBegin(spec, 1, 6): filter op 6 == "match any" over the building
    // array. One filter slot, op 6.
    static const PersonFilter kMatchAny[1] = { { 6, 0 } };
    for (ObjectRec* b = PersonQueryBegin(kMatchAny, 1); b; b = PersonIterNext()) {
        if (g_hooks->buildingFreeAndUnlink)
            g_hooks->buildingFreeAndUnlink(b);
        ++swept;
    }
    if (g_hooks->releaseTables)
        g_hooks->releaseTables();
    return swept;
}

} // namespace guild::sim
