#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex (80-byte engine record), Polygon, MeshGeometry
#include "render/sky.h"              // SkyAmbient (BlendBandLighting output triple)

// =============================================================================
// guild::render — ATMOS LIGHTING-TABLE REBUILD: the brightness-consuming
// vertex-light rebuild of the day/night cycle. This is the missing application
// link the session-atmos slice identified: the original applies day/night
// brightness by REBUILDING the per-object vertex lighting table (NOT by any
// frame post-process). Reconstructed 1:1 from the Hex-Rays decompile:
//
//   0x5b85e4  VIBE_SkyColor_BlendBandLighting   (the GLOBAL AMBIENT STORE half:
//             flt_64A074/78/7C = the band-lerped RGB, flt_64A070 = its luma; the
//             lerp math itself is render::BlendBandLighting in sky.cpp; the fog
//             rows + dome walk live in play::SessionAtmos. At its tail it calls
//             VIBE_Light_RefreshAllObjects(force) — reconstructed below.)
//   0x5c886c  VIBE_Light_RefreshAllObjects      (the rebuild trigger: serial
//             bump + invalidate walk (force<=1) or eager rebuild walk (force>1)
//             + Floor_BuildTilePolys when a floor is bound)
//   0x5c7da4  VIBE_Light_RemoveCacheEntry       (per-object light-cache list
//             free/unlink — the invalidate-walk leaf)
//   0x5c8218  VIBE_Light_BuildObjectCache       (THE LIGHTING-TABLE REBUILD:
//             seed every vertex's RGB accumulator (+48/+52/+56) from the global
//             ambient, accumulate affecting lights, reduce to the shade bytes
//             (+68/+69/+70) and PUBLISH them into the packed colour/light dword
//             at +64 — which is where Vertex::lightIdx (+66) gets the new shade
//             the 16bpp rasterizer interpolates across each triangle)
//   0x5c7f04  VIBE_Light_ApplyVertexShading     (the per-polygon material light
//             word pass over the rebuilt accumulators)
//   0x5add1c  VIBE_Render_ProcessSceneNode      (ONLY its three lazy-rebuild
//             trigger arms — LightAtmosEnsureNodeLit; the node draw walk itself
//             is the universe-render slice)
//   0x5b3900  VIBE_Render_BeginUniverseFrame    (only the per-frame relight
//             budget counter reset at 0x5b3982/0x5b398c)
//   0x5b3bbc  VIBE_Render_DrawUniverseAndStats  (only the frame-end
//             byte_64A068 = 0 clear at 0x5b3c19)
//   0x5c8964  VIBE_Light_ResetGlobalState       (zero the serial + budgets)
//
// HOW BRIGHTNESS REACHES THE PIXELS (the full recovered chain)
// ---------------------------------------------------------------------------
//   VIBE_DayCycle_UpdateBrightness @0x4b2504 (play::SessionAtmos::brightnessStep)
//     -> band/blend -> VIBE_SkyColor_BlendBandLighting @0x5b85e4
//        -> flt_64A074/78/7C ambient store      [LightAtmosStoreAmbient]
//        -> VIBE_Light_RefreshAllObjects(force) [LightAtmosRefreshAllObjects]
//           force<=1: free every object's light-cache list (RemoveCacheEntry
//                     walk, mask 2) + byte_64A068=1; the ++dword_64A064 serial
//                     bump makes EVERY object's cached serial stale
//           force> 1: eager BuildObjectCache walk (TraverseTree, mask 64)
//     -> per frame, VIBE_Render_ProcessSceneNode rebuilds stale nodes lazily,
//        budgeted: on-screen nodes against dword_64A05C (static 0x200=512
//        verts/frame), culled nodes against dword_64A054 (static 0x80=128)
//     -> VIBE_Light_BuildObjectCache seeds each vertex's accumulator triple from
//        the ambient, reduces it to the shade byte(s) and copies the packed
//        dword +68..+71 over +64..+67 — so Vertex::lightIdx (+66) = byte +70
//        (the luma shade / reduced R) — and the software rasterizer
//        (raster.cpp FillTexturedSpansShaded, fed through RasterVertex::light)
//        interpolates exactly that byte into the framebuffer.
//   There is NO palette/gamma write and NO framebuffer modulation (rule 8: no
//   post-tint analogue is shipped; this rebuild IS the application point).
//
// SCENE-WALK STAND-IN (documented substitution, NOT silent): the original walks
// the scene graph (VIBE_SceneGraph_WalkAndInvoke/TraverseTree over off_649D64).
// The reimpl's live "scene" for this table is the registry of LightAtmosObject
// records the session renderer registers (one per drawn mesh frame); the walk
// order is registration order. The walk LEAVES (RemoveCacheEntry per object /
// BuildObjectCache per object) and every decision around them are 1:1.
//
// NAMED GAPS (rule 8 — left out, said so):
//   * VIBE_Light_PrepareObjectCache @0x5c7e58 (sun position into bone-local
//     space + the +484 radius from the frame vtable) — scene/bone coupled;
//     routed through LightAtmosHooks::prepareObjectCache (default: absent).
//   * The affecting-light collect walk (VIBE_Light_CollectAffectedObject
//     @0x5c80a0, two passes + the d3_light:cache alloc) and the per-light
//     accumulation (VIBE_Light_ApplyToCachedVertices @0x5c6f90; its per-vertex
//     point/sun cores ARE reconstructed in render/light.cpp
//     AccumulatePointLight/AccumulateDirectionalLight) — routed through
//     LightAtmosHooks::accumulateLights. Default absent == the engine's
//     "no affecting lights collected" path (v25 == 0): ambient-only relight,
//     which is exactly the day/night brightness application.
//   * The poly material record (*(poly+20), bytes +104/+108) — routed through
//     LightAtmosHooks::polyMaterial. Default absent == the engine's null-
//     material skip in ApplyVertexShading.
//   * The child-object pass WalkAndInvoke(obj+520, RecomputeForObject@0x5c7cf8,
//     mask 4) at the tail of BuildObjectCache — attached children relight via
//     their own registry entry (each drawn mesh is registered).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// The lighting-table globals (each field carries its gilde.exe address and its
// STATIC-IMAGE initial value, verified via get_bytes).
// ---------------------------------------------------------------------------
struct LightAtmosGlobals {
    float ambientLuma = 200.0f;   // flt_64A070 (0x43480000 = 200.0 static)
    float ambientR    = 200.0f;   // flt_64A074 (200.0 static; band-lerped R)
    float ambientG    = 200.0f;   // flt_64A078 (200.0 static; band-lerped G)
    float ambientB    = 200.0f;   // flt_64A07C (200.0 static; band-lerped B)
    float rebuildScratch = 0.0f;  // flt_64A06C (zeroed around each rebuild)
    i32   walkDepth      = 0;     // dword_64A050 (re-entrancy depth counter)
    u32   offscreenBudget = 128;  // dword_64A054 (static 0x80: verts/frame for culled nodes)
    u32   offscreenSpent  = 0;    // dword_64A058 (reset by BeginUniverseFrame)
    u32   onscreenBudget  = 512;  // dword_64A05C (static 0x200: verts/frame on-screen)
    u32   onscreenSpent   = 0;    // dword_64A060 (reset by BeginUniverseFrame)
    u32   rebuildSerial   = 0;    // dword_64A064 (++ per RefreshAllObjects)
    u8    invalidated     = 0;    // byte_64A068  (set by the invalidate walk;
                                  //  cleared at frame end, DrawUniverseAndStats)
    u8    relightAlways   = 0;    // byte_649D54  (0 static; force-relight toggle)
    u8    colorMode       = 0;    // byte_649D70  (0 static; 0 = luma byte branch,
                                  //  !=0 = the 3-byte colour branch)
};

LightAtmosGlobals& LightAtmos();

// gilde.exe 0x5c8964 — VIBE_Light_ResetGlobalState (engine shutdown):
//   dword_64A05C = dword_64A060 = dword_64A058 = dword_64A064 = dword_64A054 = 0.
void LightAtmosResetGlobalState();

// gilde.exe 0x5b3900 (VIBE_Render_BeginUniverseFrame @0x5b3982/0x5b398c) — the
// per-frame relight budget counter reset: dword_64A058 = dword_64A060 = 0.
void LightAtmosBeginUniverseFrame();

// gilde.exe 0x5b3bbc (VIBE_Render_DrawUniverseAndStats @0x5b3c19) — the
// frame-end invalidation clear: byte_64A068 = 0.
void LightAtmosEndUniverseFrame();

// gilde.exe 0x5b85e4 (VIBE_SkyColor_BlendBandLighting, stores at 0x5b86ff /
// 0x5b8747..0x5b876f / 0x5b87b7) — publish the band-blended ambient into the
// lighting globals: flt_64A074 = a.r, flt_64A078 = a.g, flt_64A07C = a.b,
// flt_64A070 = a.luma. `a` is render::BlendBandLighting's output (sky.cpp).
void LightAtmosStoreAmbient(const SkyAmbient& a);

// Full reset for tests: globals back to the static image, hooks cleared,
// registry emptied (registered objects are NOT freed — caller-owned).
void LightAtmosResetAll();

// ---------------------------------------------------------------------------
// Per-object light-cache list node — the heap entry of the object's affecting-
// light cache list (*(obj+488)+408 head; original node: +4 = light key,
// +8 = next; freed with VIBE_Memory_FreeDebug -> delete here).
// ---------------------------------------------------------------------------
struct LightCacheNode {
    const void*     key  = nullptr;  // +4  the cached light object
    LightCacheNode* next = nullptr;  // +8  list link
};

// ---------------------------------------------------------------------------
// LightAtmosObject — the lighting-relevant slice of the engine's object record
// (offsets in comments), one per drawn mesh frame. geom is the LOD mesh frame
// the original re-selects via VIBE_Mesh_SelectLodFrame into obj+460.
// ---------------------------------------------------------------------------
struct LightAtmosObject {
    MeshGeometry* geom = nullptr;    // obj+460  (the lighting table: its Vertex array)
    u8   flags528 = 0;               // obj+528  bit2 = cache built, bit5 -> the
                                     //          collect-walk flag byte (ctx +12)
    u8   flags529 = 0;               // obj+529  bit7 set -> ApplyVertexShading
                                     //          uses the frame fallback light word
    u8   flags530 = 0;               // obj+530  bit1 = hidden (no rebuild);
                                     //          bit0 set -> skip the flag38 edit
    u8   type533  = 4;               // obj+533  4 == lit drawable (trigger arms)
    u32  frameLightWord376 = 0;      // mesh-frame dword +376 (a2[94] fallback)
    u32  cacheSerial    = 0;         // *(obj+492)+2300 (last rebuild's dword_64A064)
    int  lastBuildFrame = 0;         // obj+68 = dword_62EB38 at last rebuild
    LightCacheNode* cacheList = nullptr; // *(obj+488)+408 affecting-light cache
    bool isFloorNode = false;        // a1 == dword_649D68 (floor excluded from arms)
};

// The poly material slice ApplyVertexShading reads (*(poly+20) record).
struct LightAtmosPolyMaterial {
    u8  flagByte104  = 0;   // material+104 (bit4 -> poly flags38 bit2)
    u32 lightWord108 = 0;   // material+108 (bit16 = recompute; low word = hi/lo)
};

// ---------------------------------------------------------------------------
// Hooks for the scene-coupled leaves (defaults inert == the engine's "absent
// subsystem" paths; see the named gaps above).
// ---------------------------------------------------------------------------
struct LightAtmosHooks {
    // VIBE_Light_PrepareObjectCache @0x5c7e58 (sun-into-bone-space + radius).
    void (*prepareObjectCache)(LightAtmosObject&, void* ctx) = nullptr;
    void* prepareCtx = nullptr;
    // The affecting-light collect + accumulate walk (0x5c80a0 + 0x5c6f90).
    // `collectFlag` is the ctx byte +12: (u8)(4 * obj.flags528) >> 7 (bit5).
    void (*accumulateLights)(LightAtmosObject&, u8 collectFlag, void* ctx) = nullptr;
    void* lightsCtx = nullptr;
    // The poly material record (*(poly+20)); null return == no material (skip).
    const LightAtmosPolyMaterial* (*polyMaterial)(const LightAtmosObject&,
                                                  const Polygon&, i32 polyIndex,
                                                  void* ctx) = nullptr;
    void* materialCtx = nullptr;
    // VIBE_Floor_BuildTilePolys @0x5bc45c (terrain relight) when a floor is
    // bound (dword_64A028 != 0). Returns the walk result byte.
    u8 (*floorBuildTilePolys)(void* floor) = nullptr;
    void* floor = nullptr;           // dword_64A028 (0 static = no floor bound)
};

void SetLightAtmosHooks(const LightAtmosHooks& h);
const LightAtmosHooks& GetLightAtmosHooks();

// ---------------------------------------------------------------------------
// Vertex accumulator access — the engine's 80-byte Vertex record holds the
// RGB light accumulator at +48/+52/+56 (floats [12]/[13]/[14]; R/G/B — the
// seed order flt_64A074/78/7C) and the shade bytes at +68/+69/+70 (B at +68,
// G at +69, R/luma at +70). The publish copy stores dword +68..71 over
// +64..67, so lightIdx (+66) = byte +70. Exposed for tests/wiring.
// ---------------------------------------------------------------------------
void  VertexLightAccumSet(Vertex& v, float r, float g, float b);
void  VertexLightAccumGet(const Vertex& v, float out[3]);   // {r,g,b}
void  VertexLightAccumAdd(Vertex& v, float r, float g, float b);

// gilde.exe 0x5c7da4 — VIBE_Light_RemoveCacheEntry(obj, key). Operates on the
// object's affecting-light cache list:
//   key == null : free the WHOLE list (the invalidate-walk arm, mask 2 / a2==0)
//   else        : unlink+free the first node whose key matches (head arm /
//                 scan arm); a missing key is a no-op. Always returns 1.
char LightAtmosRemoveCacheEntry(LightAtmosObject& o, const void* key);

// gilde.exe 0x5c8218 — VIBE_Light_BuildObjectCache: THE LIGHTING-TABLE REBUILD.
//   * null mesh frame -> 0 (the SelectLodFrame-miss arm); hidden (flags530
//     bit1) -> 0;
//   * ++dword_64A050; PrepareObjectCache hook; flt_64A06C scratch;
//   * SEED: every vertex accumulator (+48/52/56) = flt_64A074/78/7C;
//   * accumulate hook (collect walk + ApplyToCachedVertices; default: none);
//   * REDUCE (byte_649D70): colour branch — max-channel scale-to-255 when the
//     max's float bits exceed 1132396544 (0x437EFFFF == 254.999985f), truncate
//     to bytes +70/+69/+68 (R/G/B); luma branch — trunc(g*0.58999997 +
//     r*0.30000001 + b*0.10999999), unsigned-saturated to 255, byte +70 only;
//     both branches then PUBLISH dword +64 = dword +68 (lightIdx = byte +70);
//   * VIBE_Light_ApplyVertexShading pass (below);
//   * flags528 |= 4; obj+68 = frameStamp (dword_62EB38); cacheSerial =
//     dword_64A064; --dword_64A050; return 1.
char LightAtmosBuildObjectCache(LightAtmosObject& o, int frameStamp);

// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading over the rebuilt
// accumulators. Per polygon with a material record:
//   * (flags530 bit0 clear) poly.flags38 = (flags38 & ~4) | (material+104
//     bit4 -> bit2);
//   * lightWord = (flags529 bit7 clear) ? material+108 : frame word +376;
//   * word bit16 set: per poly vertex, shade = trunc(min(255, (r*0.30 +
//     g*0.59 + b*0.11 + HIBYTE(word)) * (1/256) * LOBYTE(word))), broadcast
//     into the packed dwords at +64 AND +68 (lightIdx + colour bytes);
//   * else if LOBYTE(word) != 0xFF: vertex bytes +67 and +71 = LOBYTE(word).
char LightAtmosApplyVertexShading(LightAtmosObject& o);

// ---------------------------------------------------------------------------
// Registry — the scene-graph membership stand-in for the two walk arms of
// RefreshAllObjects (see the header block). Registration order == walk order.
// ---------------------------------------------------------------------------
void LightAtmosRegisterObject(LightAtmosObject* o);
void LightAtmosUnregisterObject(LightAtmosObject* o);
int  LightAtmosRegisteredCount();

// gilde.exe 0x5c886c — VIBE_Light_RefreshAllObjects(force): the rebuild
// trigger the brightness step fires (BlendBandLighting tail @0x5b88cf).
//   ++dword_64A064 (ALWAYS — even force==0 staleness-bumps every cache);
//   ++dword_64A050;
//   if (force || byte_649D54):
//     force <= 1 : invalidate walk — RemoveCacheEntry(obj, null) per object,
//                  byte_64A068 = 1;
//     force >  1 : eager walk — BuildObjectCache(obj) per object;
//   floor bound  : result = Floor_BuildTilePolys(floor);
//   --dword_64A050; return result (the incoming force when no arm ran).
u8 LightAtmosRefreshAllObjects(u8 force, int frameStamp);

// gilde.exe 0x5add1c — the three lazy-rebuild trigger arms of
// VIBE_Render_ProcessSceneNode (the per-node draw-walk consumer):
//   freshFrame (flags528 bit6 arm @0x5adfc3): type==4, not the floor node,
//     rebuild when dword_64A064 > cacheSerial || byte_64A068 (no budget);
//   onScreen   (@0x5adf07): cacheSerial < dword_64A064 and the on-screen
//     budget holds (dword_64A05C > dword_64A060) -> dword_64A060 += vertex
//     count, rebuild;
//   culled     (@0x5ae25f): serial stale and dword_64A054 > dword_64A058 ->
//     dword_64A058 += vertex count, flags528 |= 4, rebuild.
// Returns 1 when a rebuild ran.
char LightAtmosEnsureNodeLit(LightAtmosObject& o, bool freshFrame,
                             bool onScreen, int frameStamp);

} // namespace guild::render
