// ===========================================================================
// object_lifecycle2.cpp — VIBE_Object_* lifecycle / transform / fill / occupant
// record mutators. See object_lifecycle2.h for the module overview, the
// SceneObject byte-offset map, and the per-function provenance.
// ===========================================================================
#include "sim/object_lifecycle2.h"

#include <cmath>
#include <cstring>

#include "sim/building.h"             // g_buildingTypes / g_buildingTypesLoaded / BuildingTypeDefAt
#include "sim/building_production.h"  // g_sceneTypes / SceneTypeDefAt / g_sceneTypeRemap
#include "sim/entity.h"               // g_objects (dword_13CE298)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module storage (mirrors the original globals).
// ---------------------------------------------------------------------------
u16 g_objSpawnList[kSpawnTableProtCount] = {};   // word_13CE29C
u16 g_objSpawnCount = 0;                          // word_13CE860
i8  g_objSpawnOwner[kSpawnTableProtCount] = {};   // owner byte of dword_122DDAC stride
i8  g_objSlotOwner[kSpawnTableSlotCount]  = {};   // owner byte of dword_122DD5D stride
SceneObject* g_objActiveHandle = nullptr;         // dword_649D7D

namespace {
ObjLifeHooks g_hooks{};

// 0x500174 emitter seed factors (flt_620BF4 / flt_620BF8 / flt_620BFC).
constexpr float kEmitF1 = 0.05000000074505806f;  // flt_620BF4
constexpr float kEmitF2 = 0.800000011920929f;     // flt_620BF8
constexpr float kEmitF3 = 0.5f;                    // flt_620BFC

// Faithful |x| == 0 test (the original masks the sign bit: x & 0x7FFFFFFF == 0).
inline bool floatBitsZero(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return (bits & 0x7FFFFFFFu) == 0u;
}

// VIBE_Math_VectorWithinTolerance (0x5caa4c): per-component |b-a| <= tol.
inline bool vectorWithinTolerance(const float* a, const float* b, float tol) {
    return std::fabs(b[0] - a[0]) <= tol &&
           std::fabs(b[1] - a[1]) <= tol &&
           std::fabs(b[2] - a[2]) <= tol;
}

// Does `name` begin with `prefix` (the original uses VIBE_Util_StrncmpN, which
// returns 0 on a match)? Returns true on a prefix match.
inline bool startsWith(const char* name, const char* prefix) {
    return std::strncmp(name, prefix, std::strlen(prefix)) == 0;
}
}  // namespace

void ObjLifeSetHooks(const ObjLifeHooks& hooks) { g_hooks = hooks; }
void ObjLifeResetHooks() { g_hooks = ObjLifeHooks{}; }

// ---------------------------------------------------------------------------
// gilde.exe 0x583a2c — VIBE_Object_IsBuildingType
// ---------------------------------------------------------------------------
bool ObjectIsBuildingType(i16 type) {
    // v1 = *(BYTE*)(65 * type + dword_13CE27C);   (g_sceneTypes[type].kind)
    SceneTypeDef* def = SceneTypeDefAt(type);
    u8 v1 = def ? def->kind : 0;
    return v1 == 1 || v1 == 26 || v1 == 11 || v1 == 28 ||
           v1 == 27 || v1 == 3 || v1 == 4;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x586508 — VIBE_Object_CollectMatchingProts
// ---------------------------------------------------------------------------
int ObjectCollectMatchingProts(u8 buildingTypeByte, const i8* remap) {
    // if (!dword_13CE27C) return 1;  — the original guards on the SCENE type
    // base being loaded (dword_13CE27C). When absent there is nothing to scan.
    if (!g_sceneTypesLoaded) {
        return 1;
    }
    // 0x586531: `movsx edx, byte ptr [edi]` — the building type byte is read
    // SIGNED (sign-extended) for both the >= compare and the kind index. Match
    // the sign-extension faithfully (matters only for type bytes >= 128).
    const int v5signed = static_cast<int>(static_cast<i8>(buildingTypeByte));
    const BuildingTypeDef* selfDef = BuildingTypeDefAt(static_cast<u8>(v5signed));
    u8 selfKind = selfDef ? selfDef->kind : 0;  // *(dword_13CE294 + 589*v5)

    i16 count = 0;
    for (int i = 0; i < kSpawnTableProtCount; ++i) {
        int v4 = remap[i];  // LOBYTE = byte_13CE862[i]; then v4 = (char)v4
        if (static_cast<u8>(v4) == 72) {  // (_BYTE)v4 != 72 guard
            continue;
        }
        // v5 = (char)*a1; compare v5 >= (char)remap (signed) and the kinds.
        const BuildingTypeDef* remapDef = BuildingTypeDefAt(static_cast<u8>(v4));
        u8 remapKind = remapDef ? remapDef->kind : 0;  // *(589*v4 + base)
        if (v5signed >= v4 && remapKind == selfKind) {
            g_objSpawnList[static_cast<u16>(count)] = static_cast<u16>(i);
            ++count;
        }
    }
    g_objSpawnCount = static_cast<u16>(count);
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4ffee8 — VIBE_Object_ResetSpawnTables
// ---------------------------------------------------------------------------
char ObjectResetSpawnTables() {
    for (int i = 0; i < kSpawnTableProtCount; ++i) g_objSpawnOwner[i] = -1;
    for (int j = 0; j < kSpawnTableSlotCount; ++j) g_objSlotOwner[j] = -1;
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5a8140 — VIBE_Object_RebuildModelByOwner
// ---------------------------------------------------------------------------
char ObjectRebuildModelByOwner(SceneObject* model) {
    // for (i = 0; i != 43264; i += 169) { v3 = base + i; if (*v3 && model+512 ==
    //   *(v3+1)) VIBE_Object_BuildModelName(model, v3, 2); }
    for (int idx = 0; idx < kObjectCapacity; ++idx) {
        ObjectRec* obj = &g_objects[idx];
        if (obj->alive && model->ownerKey == obj->id) {
            if (g_hooks.buildModelName) {
                g_hooks.buildModelName(model, obj, 2);
            }
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5a8184 — VIBE_Object_ClearVisualFlag
// ---------------------------------------------------------------------------
char ObjectClearVisualFlag(SceneObject* node) {
    node->flags531 &= static_cast<u8>(~4u);  // *(node+531) &= ~4
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x43f434 — VIBE_Object_CmdSetActiveHandle
// ---------------------------------------------------------------------------
int ObjectCmdSetActiveHandle(SceneObject* handle) {
    g_objActiveHandle = handle;  // dword_649D7D = *a1
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x43fc8c — VIBE_Object_SetBlocked
// ---------------------------------------------------------------------------
int ObjectSetBlocked(SceneObject* node, u8 flag) {
    if (node) {
        u8 v3 = flag & 1u;                                   // *a2 & 1
        u8 v4 = static_cast<u8>(node->flags530 & 0xEFu);     // & 0xEF (clear bit4)
        node->flags530 = static_cast<u8>((16u * v3) | v4);   // (16*v3) | v4
        return 1;
    }
    if (g_hooks.reportError) g_hooks.reportError("SetBlocked: Illegal object...");
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x43fcd8 — VIBE_Object_SetTransient
// ---------------------------------------------------------------------------
int ObjectSetTransient(SceneObject* node, u8 flag) {
    if (node) {
        u8 v3 = flag & 3u;                                   // *a2 & 3
        u8 v4 = static_cast<u8>(node->flags530 & 0xF3u);     // & 0xF3 (clear 2-3)
        node->flags530 = static_cast<u8>((4u * v3) | v4);    // (4*v3) | v4
        return 1;
    }
    if (g_hooks.reportError) g_hooks.reportError("SetTransient: Illegal object...");
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x43e79c — VIBE_Object_SetAngle  (edx=x, ecx=z, ebx=y)
// ---------------------------------------------------------------------------
int ObjectSetAngle(SceneObject* node, float x, float z, float y) {
    if (node) {
        const float f1 = kPi;            // flt_617380
        const float f2 = kDegPerRadInv;  // flt_617384
        // *(node+132) = *a2 * F1 * F2;   (a2 = x)
        node->angle[0] = x * f1 * f2;
        // *(node+136) = *a4 * F1 * F2;   (a4 = y)
        node->angle[1] = y * f1 * f2;
        // *(node+140) = F1 * *a3 * F2;   (a3 = z)
        node->angle[2] = f1 * z * f2;
        node->flags528 |= 4u;            // *(node+528) |= 4
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ecmd_SetAngle: Could not find object to set the angle...");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x500134 — VIBE_Object_UnlinkFromChain
// ---------------------------------------------------------------------------
SceneObject* ObjectUnlinkFromChain(SceneObject* node) {
    i32 v2 = node->ownerKey;             // *(node+512)
    SceneObject* result = node->parent;  // *(node+504)
    if (v2 == 0) {
        do {
            if (!result) break;
            node->ownerKey = result->ownerKey;   // node+512 = result+512
            result = result->parent;             // result = result+504
        } while (node->ownerKey == 0);           // while (!node+512)
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x500174 — VIBE_Object_InitParticleEmitters
// ---------------------------------------------------------------------------
SceneObject* ObjectInitParticleEmitters(SceneObject* node, Emitter emitters[7],
                                        NodeEmitter* own) {
    if (node->nodeType == 6) {
        // for (i = 0; i != 392; i += 56) over the 7 emitters; e[8] = life,
        // e[6]/e[7] = spawn bounds, e[7]@+28 -> index 7, e[9]@+36 -> index 9.
        if (emitters) {
            for (int i = 0; i < 7; ++i) {
                float* e = emitters[i].f;
                if (floatBitsZero(e[8])) {
                    e[8] = (e[6] + e[7]) * kEmitF1;
                    e[7] = e[7] * kEmitF2;   // +28 == dword[7]
                    e[9] = e[9] * kEmitF3;   // +36 == dword[9]
                }
            }
        }
        // The node's own emitter (node+92/+96/+100/+148).
        if (own && floatBitsZero(own->life)) {
            own->life  = (own->spawnA + own->spawnB) * kEmitF1;  // +100
            own->spawnB = own->spawnB * kEmitF2;                  // +96
            own->decay = own->decay * kEmitF3;                    // +148
        }
    }
    return node;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x506388 — VIBE_Object_HideFoliageDecor
// ---------------------------------------------------------------------------
char ObjectHideFoliageDecor(SceneObject* node, int textureSet) {
    // if (!strncmp(name,"pfl_",4) || !strncmp(name,"vg_",3) ||
    //     (v4 = strncmp(name,"!vg_",4)) == 0) selectTextureSet(...).
    bool hide = startsWith(node->name, "pfl_") ||
                startsWith(node->name, "vg_") ||
                startsWith(node->name, "!vg_");
    if (hide) {
        if (g_hooks.selectTextureSet) g_hooks.selectTextureSet(node, textureSet);
        return 1;  // v4 == 0 on a match; SelectTextureSet's LOBYTE result
    }
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x506430 — VIBE_Object_HideFoliageByState
// ---------------------------------------------------------------------------
char ObjectHideFoliageByState(SceneObject* node, u8 buildingTypeByte) {
    const BuildingTypeDef* def = BuildingTypeDefAt(buildingTypeByte);
    u8 kind     = def ? def->kind : 0;          // *(589*type + base)
    u8 security = def ? def->security : 0;       // *(589*type + base + 583)
    int v5 = static_cast<int>(security) - 1;     // security - 1
    if (kind == 2) {
        v5 = static_cast<int>(security);         // kind==2 -> use security
    }
    u8 vs = node->visualState;                   // *(node+535)
    if (vs == 2 || vs == 4 || vs == 3) {
        return 1;                                // already hidden/upgrading
    }
    bool isFoliageName = startsWith(node->name, "pfl_") ||
                         startsWith(node->name, "vg_") ||
                         startsWith(node->name, "!vg_");
    if (!isFoliageName) {
        for (int i = 0; v5 >= i; ++i) {          // for (i=0; v5>=i; ++i)
            if (g_hooks.selectTextureSet) g_hooks.selectTextureSet(node, i);
        }
    }
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b0e64 — VIBE_Object_IsNearDoor (pure-geometry core)
// ---------------------------------------------------------------------------
bool ObjectIsNearDoorCore(const DoorProximityInputs& in) {
    // v3 = *(node+388); if (*(target+97) && v3) { v4 = FindByHandle(...,"dummy_TUER");
    //   if (!v4) v4 = *(target+97); PointThroughBoneChain(v4,...,v6);
    //   if (VectorWithinTolerance(v3[13]+76, v6, 750.0)) { if (!v3[74]) return 1; } }
    // return v3 && v3[11] == *(target+1);
    if (in.targetHasAnchor && in.hasActor) {
        if (vectorWithinTolerance(in.doorPos, in.testPos, in.tolerance)) {
            if (!in.doorOpenFlag) {
                return true;
            }
        }
    }
    return in.hasActor && in.actorOwnerId == in.targetOwnerId;
}

}  // namespace guild::sim
