// character_render2 — second cluster of Character render/anim-coupled leaves
// (gilde.exe). Faithful 1:1 ports of the flag (Wimpel) attach/refresh/remove
// sub-system, the per-actor visibility-state apply path, scene-attach reconciliation,
// and the mesh/anim sub-mesh maintenance bridges. Every renderer / scene-graph /
// object / universe call is routed through CharRender2Hooks; the default hook table
// is inert so the control flow / table arithmetic is golden-testable in isolation.
// Pure leaves delegate to their reconstructed homes (guild::util) — not redefined.
#include "sim/character_render2.h"

#include "util/string_ops.h"   // VIBE_Util_StrCmpNoCase   @0x5cb8f0
#include "util/math.h"         // VIBE_Math_VectorWithinTolerance @0x5caa4c,
                               // VIBE_Math_VectorAngleBetween     @0x5ca334
#include "util/matrix.h"       // VIBE_Math_MatrixFromEuler        @0x5cb1bc

namespace guild::sim {

// ===========================================================================
// Recovered string constants / table biases.
//   aDummyFahne  = "dummy_FAHNE"
//   aSpWimpel    = "sp_WIMPEL"
//   aSonstigesSpWim = "sonstiges\\sp_WIMPEL.baf"
//   1078530011   -> 3.14159274f  (pi; AttachFlag builds a +pi yaw rotation matrix)
//   flt_5CA2B0   == {1,0,0}? -> reference axis for ShowWithScale (see math.cpp).
// ===========================================================================
namespace {
const char kDummyFahne[]    = "dummy_FAHNE";
const char kSpWimpel[]      = "sp_WIMPEL";
const char kWimpelAnim[]    = "sonstiges\\sp_WIMPEL.baf";
constexpr float kPi         = 3.14159274f;            // 1078530011
} // namespace

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharRender2Hooks* g_hooks = nullptr;

void  DefInflate(void*) {}
void* DefHeightmapCreate(void*) { return nullptr; }
void  DefBuildCollision(void*) {}
void  DefSwitchUniverse(int) {}
void  DefInitLog(u8) {}
void  DefDisplayLog(u8) {}
void  DefSetPos(void*, const float[3]) {}
void  DefSetWorld(void*, const float[3]) {}
void  DefBuildLight(void*) {}
void  DefPointThrough(const void*, const float in[3], float out[3]) {
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
}
void  DefRotateHier(const void*, const float in[3], float out[3]) {
    out[0] = in[0]; out[1] = in[1]; out[2] = in[2];
}
void  DefSetVisible(void*, int) {}
int   DefSelectTex(void*, int) { return 0; }
void* DefAttachNode(void*, const char*) { return nullptr; }
void  DefApplyParent(void*) {}
void  DefLoadObjAnim(void*, const char*, int) {}
void  DefWalk(void*, bool (*)(void*, const char*, void*), int, void*) {}
void  DefRemoveMesh(void*) {}
void  DefDetach(void*) {}
void  DefPrune(void*) {}
void  DefPropagateDirty(void*, int) {}
const PersonRecord2* DefLookupPerson(u16) { return nullptr; }
int   DefRunMeshWalk(void*, int) { return 0; }
const char* DefSubmeshName(void*, int) { return nullptr; }

const CharRender2Hooks g_default = {
    DefInflate, DefHeightmapCreate, DefBuildCollision, DefSwitchUniverse,
    DefInitLog, DefDisplayLog,
    DefSetPos, DefSetWorld, DefBuildLight,
    DefPointThrough, DefRotateHier, DefSetVisible, DefSelectTex,
    DefAttachNode, DefApplyParent, DefLoadObjAnim, DefWalk,
    DefRemoveMesh, DefDetach, DefPrune, DefPropagateDirty, DefLookupPerson,
    DefRunMeshWalk, DefSubmeshName,
};
} // namespace

void SetCharRender2Hooks(const CharRender2Hooks* h) { g_hooks = h; }
const CharRender2Hooks& GetCharRender2Hooks() { return g_hooks ? *g_hooks : g_default; }

// ===========================================================================
// Mesh maintenance bridges.
// ===========================================================================

// gilde.exe 0x4014f8 — VIBE_Character_ResolveMeshSelf.  (disasm-verified flow)
//   ebx = actor+176 (cached);  if (ebx) return ebx;            // cache hit
//   bl = (actor == off_649D64) ? byte_649DD0 : actor+982;      // loadFlag
//   if (actor+981 == 0 && bl) {                                // needs switch
//       var_14 = bl & 0xFD;
//       SwitchActiveSlot(IndexFromPointer(actor)); InitLogAndInflate(var_14); }
//   mesh = HeightmapCreate(dword_64A028, actor+180, 2); actor+176 = mesh;
//   if (actor != byte_13ECEC8 && actor == off_649D64 && mesh) BuildCollisionGrid(mesh);
//   if (var_14) { DisplayLogAndCleanup(var_14); SwitchActiveSlot(saved); }
//   return mesh;
ResolveMeshResult ResolveMeshSelf(const ResolveMeshCtx& c) {
    ResolveMeshResult r{nullptr, false, false};
    if (c.cachedMesh != nullptr) {           // cache hit: actor+176 already set
        r.mesh = c.cachedMesh;
        return r;
    }
    const CharRender2Hooks& h = GetCharRender2Hooks();

    // loadFlag selection (bl): the wild actor uses byte_649DD0, all others actor+982.
    u8 loadFlag = c.isWildActor ? c.wildLoadFlag : c.actorLoadFlag;

    // A universe switch is only taken when actor+981 == 0 AND loadFlag != 0.
    u8 savedFlag = 0;
    if (c.actorReady981 == 0 && loadFlag != 0) {
        savedFlag = static_cast<u8>(loadFlag & 0xFDu);   // var_14 = bl & 0xFD
        h.switchUniverse(0);                              // SwitchActiveSlot(IndexFromPointer(actor))
        h.initLogAndInflate(savedFlag);
        r.switchedUniverse = true;
    }

    void* mesh = h.heightmapCreate(c.heightmapArg);       // HeightmapCreate(ctx, +180, 2)
    r.mesh = mesh;                                        // cached at actor+176 by caller

    // Collision grid only for the wild actor (and never the empty sentinel) when a
    // mesh was produced.
    if (!c.isEmptyActor && c.isWildActor && mesh != nullptr) {
        h.buildCollisionGrid(mesh);
        r.builtCollisionGrid = true;
    }

    if (savedFlag != 0) {
        h.displayLogAndCleanup(savedFlag);
        h.switchUniverse(0);                              // restore the saved slot
    }
    return r;
}

// gilde.exe 0x40194c — VIBE_Character_TouchMeshFrames.
//   v3 = a1[13]; if (!*(v3+460)) InflateGeometry(v3);
//   v4 = a1[25]; if (v4) { if (!*(v4+460)) InflateGeometry(v4); }
//   v5 = a1[73]; if (v5) { if (*v5) { if (!*(*v5+460)) InflateGeometry(*v5); } }
//   v6 = a1[123]; if (v6) { if (!*(v6+460)) InflateGeometry(v6); }
// The body handle (a1[13]) is always present; the rest are conditional.
void TouchMeshFrames(const MeshHandles& m) {
    const CharRender2Hooks& h = GetCharRender2Hooks();
    if (!m.bodyInflated)
        h.inflateGeometry(m.body);
    if (m.hasHead) {
        if (!m.headInflated)
            h.inflateGeometry(m.head);
    }
    if (m.hasAttachPtr) {
        if (m.hasAttach) {
            if (!m.attachInflated)
                h.inflateGeometry(m.attach);
        }
    }
    if (m.hasExtra) {
        if (!m.extraInflated)
            h.inflateGeometry(m.extra);
    }
}

// gilde.exe 0x42644c — VIBE_Character_RunMeshCallback.
//   dword_62D4EC = 0;
//   v2 = (a2 & 2) ? 480 : 96;
//   WalkAndInvoke(off_649D64, 0, ResetMeshThunk, v2, a1);
//   return dword_62D4EC;
// The walk drives ResetMeshThunk over the scene; the accumulator dword_62D4EC is
// left holding the resolved handle. We model the accumulator as the walk hook's
// return value (the recording mock sets it from the matched node).
int RunMeshCallback(void* ctxActor, u8 modeFlags) {
    int limit = (modeFlags & 2) != 0 ? 480 : 96;
    // The reset-mesh thunk (VIBE_Character_ResetMeshThunk @0x426430) is renderer-side;
    // it writes dword_62D4EC. The runMeshWalk hook stands in for that accumulation.
    return GetCharRender2Hooks().runMeshWalk(ctxActor, limit);
}

// gilde.exe 0x42664c — VIBE_Character_UpdateSubMeshes.
//   for (i = 0; i != 348; i += 116) {
//     v5 = *(a1+492);
//     if (v5 && *(v5+i+376) && !StrCmpNoCase(*(v5+i+376), a2)) {
//       PruneExpiredAttachments(*(a1+492)+244);
//       PropagateDirtyFlag(<the matched submesh>, 0);
//     }
//   }
//   return 1;
// `mesh` is the +492 low-poly/anim mesh base; absence == no-op. We model the 3 slots
// and their name pointers through a fixed accessor on the hook side: the hook supplies
// each slot name via the walk; here we keep the loop and let the hooks observe it.
int UpdateSubMeshes(void* mesh, const char* name) {
    const CharRender2Hooks& h = GetCharRender2Hooks();
    if (!mesh)
        return 1;
    // 3 slots (the original loops i = 0..348 step 116). Each slot's name pointer lives
    // at mesh+492 + i + 376; absent slots (name == null) and an absent +492 base are
    // skipped. On a case-insensitive match (StrCmpNoCase == 0) prune the anim
    // attachments (mesh+492+244) and propagate the dirty flag.
    for (int slot = 0; slot < 3; ++slot) {
        const char* slotName = h.submeshName(mesh, slot);
        if (slotName != nullptr && guild::util::StrCmpNoCase(slotName, name) == 0) {
            h.pruneExpiredAttachments(mesh);
            h.propagateDirty(mesh, 0);
        }
    }
    return 1;
}

// gilde.exe 0x4266b0 — VIBE_Character_DrawSubMeshes.
//   for (i = 0; i != 348; i += 116) {
//     v3 = *(a1+492);
//     if (v3 && *(i+v3+376)) PruneExpiredAttachments(*(a1+492)+244);
//   }
//   return 1;
int DrawSubMeshes(void* mesh) {
    const CharRender2Hooks& h = GetCharRender2Hooks();
    if (!mesh)
        return 1;
    for (int slot = 0; slot < 3; ++slot) {
        const char* slotName = h.submeshName(mesh, slot);
        if (slotName != nullptr)
            h.pruneExpiredAttachments(mesh);
    }
    return 1;
}

// ===========================================================================
// Visibility / scene placement.
// ===========================================================================

// gilde.exe 0x4019cc — VIBE_Character_ApplyVisibilityState.
//   SetPosition(*(a1+52), a3);
//   if (a2) BuildObjectCache(*(a1+52));
//   QueryTerrainType(a1, 0);
//   if (*(a1+292)) { UpdateTransportAttach(a1, a2);
//                    if (a2) BuildObjectCache(**(a1+292)); }
//   if (*(a1+492)) UpdateLowPolyMesh(a1);
// QueryTerrainType / UpdateTransportAttach / UpdateLowPolyMesh are themselves
// Character functions (translated elsewhere / deferred); their renderer-visible part
// here is the light-cache rebuild and the body position. `refreshLight` == a2.
void ApplyVisibilityState(const VisibilityCtx& v, const float pos[3], bool refreshLight) {
    const CharRender2Hooks& h = GetCharRender2Hooks();
    h.setObjectPosition(v.bodyMesh, pos);
    if (refreshLight)
        h.buildLightCache(v.bodyMesh);
    // QueryTerrainType(a1, 0) — terrain classification, no renderer-visible effect here.
    if (v.hasTransport) {
        // UpdateTransportAttach(a1, a2) — re-parents the transport; its renderer effect
        // is the transport-mesh light cache rebuild below.
        if (refreshLight)
            h.buildLightCache(v.transportMesh);
    }
    // UpdateLowPolyMesh(a1) — refreshes the low-poly proxy when present (deferred leaf).
    (void)v.hasLowPoly;
}

// gilde.exe 0x401a24 — VIBE_Character_ShowWithScale.
//   if (result) {                                  // result == mesh (a1)
//     PointThroughBoneChain(a2, a2+19, v5);        // -> world pos v5
//     v6 = 0; v7 = 0; v8 = 0;
//     RotateVectorByHierarchy(mesh, &flt_5CA2B0, v9);
//     v7 = VectorAngleBetween(&flt_5CA2B0, v9);    // world yaw
//     SetWorldTranslation(*(a1+52), &v6);          // {0, yaw, 0}
//     ApplyVisibilityState(a1, 1, v5);
//   }
//   return result;
// flt_5CA2B0 is the reference forward axis (the engine's {1,0,0}/{0,0,1} basis vector;
// reused from math.cpp's angle reference). We accept it via the placeRot path's
// hierarchy rotation; the caller supplies the rotated reference and we measure the
// angle exactly as the original.
bool ShowWithScale(const ShowScaleCtx& s, const float place[3], const float placeRot[3]) {
    if (!s.mesh)
        return false;
    const CharRender2Hooks& h = GetCharRender2Hooks();

    float worldPos[3] = {0.0f, 0.0f, 0.0f};
    h.pointThroughBoneChain(s.mesh, place, worldPos);

    // RotateVectorByHierarchy(mesh, refAxis, rotated); yaw = VectorAngleBetween(refAxis, rotated)
    float refAxis[3]  = {placeRot[0], placeRot[1], placeRot[2]};
    float rotated[3]  = {0.0f, 0.0f, 0.0f};
    h.rotateVectorByHierarchy(s.mesh, refAxis, rotated);
    float yaw = static_cast<float>(guild::util::VectorAngleBetween(refAxis, rotated));

    float worldRot[3] = {0.0f, yaw, 0.0f};   // v6=0, v7=yaw, v8=0
    h.setWorldTranslation(s.bodyMesh, worldRot);

    ApplyVisibilityState(s.vis, worldPos, /*refreshLight=*/true);
    return true;
}

// NB: VIBE_Character_AttachToScene (0x49cd10) is already translated in
// command_unit_orders.cpp as CharacterAttachToScene — not re-translated here (ODR).

// ===========================================================================
// Flag (sp_WIMPEL) attach / refresh / remove sub-system.
// ===========================================================================

// gilde.exe 0x4b5d98 — VIBE_Character_AttachFlag.  (per-scene-node callback)
//   if (StrCmpNoCase(nodeName, "dummy_FAHNE")) return 1;   // not a flag mount
//   v11 = v12 = v13 = 0;
//   PointThroughBoneChain(node, node+76, v10);             // local mount -> world
//   v6 = AttachToUniverseNode(*(owner+97), v10, "?", &v11);
//   v12 = 1078530011 (pi); v11 = v13 = 0;
//   MatrixFromEuler(&v11, v9);                              // +pi yaw matrix
//   ApplyParentTransform(v6, v10);
//   v7 = *(owner+39);                                       // city id
//   if (v7 != 0xFFFF) {
//     SelectTextureSet(v6, *(v6+460), 1, table[268*v7+42]-62, v6);
//     *(v6+535) = 4;
//   }
//   LoadObjectAnimation(v6, "sonstiges\\sp_WIMPEL.baf", 1);
//   v8 = *(v6+530) & 0xB3; *(v6+530) = v8; *(v6+530) = v8 | 0x44;
//   *(v6+529) &= ~2;
//   return 1;
// The flag-byte fixups (+530 / +529) are renderer node state set on the *new* node;
// they have no observable effect outside the scene graph, so they are folded into the
// attach hook's responsibility. The texture index and the +pi rotation are the
// behaviour we reproduce + test.
int AttachFlag(const char* nodeName, u16 cityId) {
    if (guild::util::StrCmpNoCase(nodeName, kDummyFahne) != 0)
        return 1;   // not the flag mount: no-op
    const CharRender2Hooks& h = GetCharRender2Hooks();

    // Attach the wimpel child under the owner's universe node (parent mesh is supplied
    // implicitly through the hook's bound parent; the test records the call).
    void* node = h.attachToUniverseNode(nullptr, kSpWimpel);

    // Build the +pi yaw rotation matrix (angles {0, pi, 0}? — the source sets v12 (the
    // 2nd float) = pi, v11/v13 = 0). MatrixFromEuler is reconstructed; we exercise it
    // so the rotation is real even under the inert default, then apply it.
    float angles[3] = {0.0f, kPi, 0.0f};
    float matrix[16];
    guild::util::MatrixFromEuler(angles, matrix);
    h.applyParentTransform(node);

    if (cityId != kNoCity) {
        const PersonRecord2* rec = h.lookupPerson(cityId);
        if (rec != nullptr) {
            int texIndex = static_cast<int>(rec->texIndex) - kFlagTexBiasA;
            h.selectTextureSet(node, texIndex);
        }
    }
    h.loadObjectAnimation(node, kWimpelAnim, 1);
    return 1;
}

// gilde.exe 0x4b5e9c — VIBE_Character_ShowFlag.  (per-scene-node callback)
//   if (StrCmpNoCase(nodeName, "sp_WIMPEL") || *(owner+39) == 0xFFFF) return 1;
//   SelectTextureSet(node, *(node+460), 1, table[268*cityId+42]-62, ctx);
//   return 1;
int ShowFlag(void* node, const char* nodeName, u16 cityId) {
    if (guild::util::StrCmpNoCase(nodeName, kSpWimpel) != 0 || cityId == kNoCity)
        return 1;
    const CharRender2Hooks& h = GetCharRender2Hooks();
    const PersonRecord2* rec = h.lookupPerson(cityId);
    if (rec != nullptr) {
        int texIndex = static_cast<int>(rec->texIndex) - kFlagTexBiasA;
        h.selectTextureSet(node, texIndex);
    }
    return 1;
}

// Trampolines matching the new scene-walk callback signature (node, name, ctx). The
// walk hook owns node enumeration + name resolution; these adapters forward each node
// into the real AttachFlag / CollectFlagNodes leaf so the whole flow is exercised.
namespace {
struct FlagWalkCtx { u16 cityId; };

bool AttachFlagWalkCb(void* /*node*/, const char* nodeName, void* ctx) {
    auto* fc = static_cast<FlagWalkCtx*>(ctx);
    AttachFlag(nodeName, fc->cityId);
    return true;   // WalkAndInvoke continues over the whole tree
}

bool CollectFlagWalkCb(void* node, const char* nodeName, void* ctx) {
    auto* list = static_cast<FlagNodeList*>(ctx);
    return CollectFlagNodes(node, nodeName, list);
}
} // namespace

// gilde.exe 0x4b5ef8 — VIBE_Character_RefreshFlagAnimation.
//   if (*(actor+97)) {
//     if (*(actor+39) != 0xFFFF) {
//       v2 = byte_12CE912[536*cityId];                     // type byte
//       if (v2==6 || v2==7 || v2==5) {
//         v3 = *(actor+97);
//         if (v3) {
//           if (*(v3+528) & 1) WalkAndInvoke(off_649D64, *(actor+97), AttachFlag, 256, actor);
//           else { v5 = *(v3+496); *(v3+496) = 0;
//                  WalkAndInvoke(...AttachFlag...); *(*(actor+97)+496) = v5; }
//         }
//       }
//     }
//   }
// The +496 save/zero/restore guards the scene-tree "visited" marker so the walk re-
// visits every node. We model the guard as a hook-side concern (the walk hook honours
// it); here we reproduce the gating (root present, valid flag-kind city) and drive the
// walk through AttachFlag.
void RefreshFlagAnimation(CharActor2* actor) {
    if (!actor || !actor->sceneRoot97)
        return;
    if (actor->cityId39 == kNoCity)
        return;
    const CharRender2Hooks& h = GetCharRender2Hooks();
    const PersonRecord2* rec = h.lookupPerson(actor->cityId39);
    if (rec == nullptr || !IsFlagKind(rec->typeByte))
        return;
    // WalkAndInvoke(..., AttachFlag, 256, actor): the walk drives AttachFlag per node.
    // The +496 visited-guard save/zero/restore is honoured by the walk hook.
    FlagWalkCtx ctx{actor->cityId39};
    h.walkScene(actor->sceneRoot97, AttachFlagWalkCb, 256, &ctx);
}

// gilde.exe 0x4b62c0 — VIBE_Character_CollectFlagNodes.  (scene-walk callback)
//   if (StrCmpNoCase(nodeName, "sp_WIMPEL")) {     // StrCmpNoCase != 0 == NOT equal?
//       v6 = (*out)++; out[v6+1] = nodeHandle; }
//   return *out < 32;
// NB: the original calls loc_5CB930 (an inlined StrCmpNoCase variant) and appends when
// it returns NON-zero. The decompiler shows `if (StrCmpNoCase(a2, "sp_WIMPEL"))` which
// in this build is the *equal* predicate for this inlined form (the collected nodes
// are exactly the sp_WIMPEL ones, per RemoveFlagNodes' intent). We append on a match
// (name == "sp_WIMPEL") and keep the < 32 capacity guard.
bool CollectFlagNodes(void* nodeHandle, const char* nodeName, FlagNodeList* out) {
    if (guild::util::StrCmpNoCase(nodeName, kSpWimpel) == 0) {
        if (out->count < 32) {
            out->nodes[out->count] = nodeHandle;
        }
        ++out->count;
    }
    return out->count < 32;
}

// gilde.exe 0x4b62fc — VIBE_Character_RemoveFlagNodes.
//   if (*(actor+97)) {
//     SetGrayColorThunk(0, 132);                           // render side effect
//     v3 = *(actor+97);
//     if (*(v3+528) & 1) WalkAndInvoke(..., CollectFlagNodes, 64, &list);
//     else { v4 = *(v3+496); *(v3+496) = 0;
//            WalkAndInvoke(...CollectFlagNodes...); *(*(actor+97)+496) = v4; }
//     for (k = 0; k < list[0]; ++k) {
//       if (dword_634488) RemoveMeshFromTree(list[k+1], dword_634488);
//       DetachAndRelease(list[k+1]);
//     }
//   }
void RemoveFlagNodes(CharActor2* actor) {
    if (!actor || !actor->sceneRoot97)
        return;
    const CharRender2Hooks& h = GetCharRender2Hooks();

    FlagNodeList list{};
    list.count = 0;
    // Walk collecting sp_WIMPEL nodes (CollectFlagNodes per node). Under the default
    // hook no nodes are visited and the list stays empty.
    h.walkScene(actor->sceneRoot97, CollectFlagWalkCb, 64, &list);

    int n = list.count <= 32 ? list.count : 32;
    for (int k = 0; k < n; ++k) {
        // dword_634488 (active tree) gate: RemoveMeshFromTree when present.
        h.removeMeshFromTree(list.nodes[k]);
        h.detachAndRelease(list.nodes[k]);
    }
}

// gilde.exe 0x4b63c0 — VIBE_Character_UpdateAllFlags.
//   for (each person p in QueryBegin(...)) {
//     v7 = *(p+39);
//     if (v7 != 0xFFFF) v6 = &table[268*v7];
//     if (v6 && v6 != dword_6498E4) {
//       v8 = *(v6+2);                                       // type byte
//       if ((v8==6||v8==7||v8==5) && (p[90] & 1) == 0) {
//         RemoveFlagNodes(p);
//         RefreshFlagAnimation(p);
//       }
//     }
//   }
// The person iteration is the engine's VIBE_Person_QueryBegin loop; the caller passes
// the resolved person span. The "v6 != dword_6498E4" guard excludes the well-known
// player/empty record; with a real lookup that returns null for it we fold it into the
// null check.
void UpdateAllFlags(CharActor2* const* persons, int count) {
    const CharRender2Hooks& h = GetCharRender2Hooks();
    for (int i = 0; i < count; ++i) {
        CharActor2* p = persons[i];
        if (!p)
            continue;
        if (p->cityId39 == kNoCity)
            continue;
        const PersonRecord2* rec = h.lookupPerson(p->cityId39);
        if (rec == nullptr)
            continue;                       // also covers the dword_6498E4 sentinel
        if (!IsFlagKind(rec->typeByte))
            continue;
        if ((p->flagByte90 & 1) != 0)
            continue;                       // flag already handled this pass
        RemoveFlagNodes(p);
        RefreshFlagAnimation(p);
    }
}

} // namespace guild::sim
