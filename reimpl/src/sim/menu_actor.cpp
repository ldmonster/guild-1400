// menu_actor — VIBE_Character_CreateMenuDummyActor @0x52af64, the dynasty-scene animated
// ancestor-actor factory. Faithful 1:1 port of the control flow + the facing-yaw math.
// Genuine scene-graph / character-subsystem calls are routed through MenuActorHooks (inert
// defaults), mirroring sim/character_render3.cpp. The facing math reuses the already-
// reconstructed guild::util primitives (never redefined here).
#include "sim/menu_actor.h"

#include "util/transform.h"   // RotateVectorByHierarchy @0x5c8990 (REUSED)
#include "util/math.h"        // VectorAngleBetween       @0x5ca334 (REUSED)

namespace guild::sim {

// flt_5CA2B0 == {0, 0, 1} (recovered with get_bytes @0x5ca2b0).
const float kForwardZ[3] = {0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const MenuActorHooks* g_hooks = nullptr;

void* DefFindDummy(int, const char*)              { return nullptr; }
void  DefGetDummyPos(void*, float out3[3])        { out3[0] = out3[1] = out3[2] = 0.0f; }
void* DefCreateChar(const char*)                  { return nullptr; }
void* DefCharObjNode(void*)                       { return nullptr; }
void  DefSetWorldRot(void*, const float[3])       {}
void  DefQueryTerrain(void*)                      {}
void  DefLinkBackref(void*, void*)                {}
void  DefPropagateDirty(void*)                    {}
void  DefPreloadGait(void*)                       {}
void  DefRegisterStatus(void*)                    {}

const MenuActorHooks g_default = {
    DefFindDummy, DefGetDummyPos, DefCreateChar, DefCharObjNode, DefSetWorldRot,
    DefQueryTerrain, DefLinkBackref, DefPropagateDirty, DefPreloadGait, DefRegisterStatus,
};
} // namespace

void SetMenuActorHooks(const MenuActorHooks* h) { g_hooks = h; }
const MenuActorHooks& GetMenuActorHooks() { return g_hooks ? *g_hooks : g_default; }

// ===========================================================================
// gilde.exe 0x52af64 — VIBE_Character_CreateMenuDummyActor.
//
//   result = VIBE_Object_FindByHandle(0, 256, a1, 0, a2);   // find the scene dummy node
//   if (!result) return result;                             // null -> null
//   VIBE_Transform_PointThroughBoneChain(result, result+19, &v12);   // dummy world pos
//   v6 = VIBE_Character_CreateFromModel(a2);
//   if (v6) {
//     v13 = 0; v14 = 0.0; v15 = 0;                          // {0, yaw, 0}
//     VIBE_Transform_RotateVectorByHierarchy(result, &flt_5CA2B0(={0,0,1}), v16);
//     v14 = VIBE_Math_VectorAngleBetween(&flt_5CA2B0, v16); // facing yaw (the +Y slot)
//     VIBE_Object_SetWorldTranslation(*(v6+52), &v13);      // node world rotation = {0,yaw,0}
//     VIBE_Character_QueryTerrainType(v6, 0);
//     *(*(v6+52)+512) = v6;                                 // node +512 = char back-ref
//     VIBE_Object_PropagateDirtyFlag(*(v6+52), 1);
//     VIBE_Character_PreloadAniSet(v6, 1, "bewegung/gehen");
//     *(*(v6+52)+72) = 0;
//     *(v6+416) = 0x3F2AAAB3;                               // scale 0.6666667f
//     *(v6+508) = -1;                                       // dynasty unused-slot marker
//     *(v6+44)  = 1;                                        // active flag
//     *(v6+512) = (byte)4;                                  // type/category
//     VIBE_StatusText_Register(*(v6+52), 0);
//   }
//   return v6;
//
// The original threads a single `float v12` for the dummy world position (only the call
// matters; the value is unused thereafter) and a `{v13,v14,v15}` rotation vector whose +Y
// component (v14) is the computed yaw. We compute both faithfully and expose them via
// MenuActorRecord for the headless test; the actor mutations go through the hook table.
// ===========================================================================
void* CreateMenuDummyActor(int dummyName, const char* model, MenuActorRecord* rec) {
    const MenuActorHooks& h = GetMenuActorHooks();

    void* dummy = h.findDummyObject(dummyName, model);
    if (rec) rec->found = (dummy != nullptr);
    if (dummy == nullptr)
        return nullptr;                       // dummy not found -> return null

    // VIBE_Transform_PointThroughBoneChain(dummy, dummy+19, &v12): dummy world position.
    // (Computed for parity; the result is not consumed by the actor setup below.)
    float dummyWorld[3] = {0.0f, 0.0f, 0.0f};
    h.getDummyWorldPos(dummy, dummyWorld);
    if (rec) { rec->placedPos[0] = dummyWorld[0]; rec->placedPos[1] = dummyWorld[1];
               rec->placedPos[2] = dummyWorld[2]; }

    void* chr = h.createCharacterFromModel(model);
    if (rec) rec->created = (chr != nullptr);
    if (chr == nullptr)
        return nullptr;                       // character create failed -> return null

    // Rotation vector {v13, v14, v15} = {0, yaw, 0}.
    float rot[3] = {0.0f, 0.0f, 0.0f};

    // forward = RotateVectorByHierarchy(dummy, {0,0,1}); yaw = VectorAngleBetween({0,0,1}, forward).
    float forward[3] = {0.0f, 0.0f, 0.0f};
    guild::util::RotateVectorByHierarchy(reinterpret_cast<float*>(dummy),
                                         kForwardZ, forward);
    float ref[3] = {kForwardZ[0], kForwardZ[1], kForwardZ[2]};
    float yaw = static_cast<float>(guild::util::VectorAngleBetween(ref, forward));
    rot[1] = yaw;                             // v14 (the +Y slot)
    if (rec) rec->facingYaw = yaw;

    void* node = h.charObjectNode(chr);       // *(v6+52)
    h.setWorldRotation(node, rot);            // SetWorldTranslation(node, {0,yaw,0})
    h.queryTerrainType(chr);                  // QueryTerrainType(v6, 0)
    h.linkNodeBackref(node, chr);             // *(node+512) = chr
    if (rec) rec->backrefLinked = true;
    h.propagateDirtyFlag(node);               // PropagateDirtyFlag(node, 1)
    h.preloadGaitAnim(chr);                   // PreloadAniSet(v6, 1, "bewegung/gehen")
    if (rec) rec->gaitPreloaded = true;

    // *(node+72) = 0; the actor record fields:
    //   +416 scale 0.6666667f, +508 slot marker -1, +44 active 1, +512(byte) type 4.
    if (rec) {
        rec->scale      = kMenuActorScale;    // 0x3F2AAAB3 == 1060320051
        rec->slotMarker = -1;                 // dynasty unused-slot marker
        rec->active     = 1;
        rec->typeByte   = 4;
    }

    h.registerStatusText(node);               // StatusText_Register(node, 0)
    return chr;                               // the created character actor
}

} // namespace guild::sim
