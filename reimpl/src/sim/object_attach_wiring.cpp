// object_attach_wiring — see object_attach_wiring.h. GLUE only: binds the
// character FACTORY's inert attach hook to the GENUINE 1:1 reconstruction of
// VIBE_Object_AttachToUniverseNode @0x5b3e30 (guild::sim::ObjectAttachToUniverseNode,
// sim/object_lifecycle10.cpp), AND (rule 13) binds every still-inert leaf of the
// load->attach chain (ObjLife10Hooks + MeshLodHooks.allocDrawData) to its REAL
// reconstructed leaf, so a single InstallRealObjectAttachWiring() makes the whole
// ObjectAttachToUniverseNode chain run live end to end. No module logic lives here.
//
// THE LIVE CALL (gilde.exe 0x402a08, VIBE_Character_CreateMesh @0x4029c4):
//   *(rec+52) = VIBE_Object_AttachToUniverseNode(
//                   0,                      // eax  parent  -> always 0 (root attach)
//                   a3,                     // edx  pos     == the factory's parentMat
//                   v31 /*model*/,          // ecx  name
//                   (int)v30,               // ebx  worldRot/xlate == dword_401010 {0,0,0,0}
//                   (int)&v31);             // edi  lodArg  == &model (opaque; ctx)
// CharacterFactoryHooks models this as `void* attachToUniverseNode(void* parentMat,
// const char* model)` — parent is hard-zero, and the factory's `parentMat` rides
// edx as the genuine `pos` vector. This installer reproduces that exact mapping.
//
// THE CHAIN (0x5b3e30): Object_Spawn(4,name) -> SetParent -> Mesh_FindStockObject /
//   Mesh_LoadOrFindByName -> Mesh_AttachStockObjectLods -> per-LOD texture upload ->
//   SetPosition / SetWorldTranslation -> root-only LinkIntoScene; mesh-load failure
//   -> Dispose -> null. After InstallRealObjectAttachWiring() the hooks bind to:
//     objSpawn               -> the REAL kind-4 Object_Spawn @0x5b054c path
//                               (verbatim raw-byte SceneNode10 + the REAL
//                                ObjectInitStruct @0x5b0e88).
//     objSetPosition         -> the REAL SetPosition @0x5af38c record write
//                               (node+76/+80/+84 = a1[19..21]).
//     objSetWorldTranslation -> the REAL SetWorldTranslation @0x5af50c record write
//                               (node+132/+136/+140 + the +396 frame matrix via the
//                                REAL VIBE_Math_MatrixFromEuler @0x5cb1bc).
//     meshFindStockObject    -> 0 (force the load leg; LoadOrFindByName dedups).
//     meshLoadOrFind         -> the REAL Mesh_LoadOrFindByName @0x5d345c body
//                               (BuildLodFileName + Registry FindStockObject-or-
//                                LoadAndRegister @0x5d32d4, REUSE).
//     meshAttachLods         -> the REAL Mesh_AttachStockObjectLods @0x5d1824
//                               (render/mesh_lod_name.cpp), whose allocDrawData hook
//                               binds to the REAL Object_AllocDrawData @0x5b107c and
//                               whose attachStockTextures hook is the REAL
//                               AttachStockTextures @0x5d1114 (InstallStockTextureAttach).
//     allocPolysAndPoints    -> the REAL Object_AllocPolysAndPoints @0x5b0c10 alloc
//                               (raw-byte clone on the relocated per-frame draw block).
//     memAllocDebug          -> a zeroing heap allocator (VIBE_Memory_AllocDebug @0x438f10
//                               stand-in; the node owns the block for its lifetime).
//
// GAPS (rule 8) — leaves left HOOKED because they need the live universe / Vulkan
// runtime that this headless install does not provide:
//   * textureUploadToSurface (VIBE_Texture_UploadToSurface @0x5db234) — uploads each
//     resolved surface to the GPU; needs the live Vulkan IGraphicsDevice + the
//     dword_1406A84 texture-record table. Stays inert: the chain still walks every
//     surface slot (the upload loop runs) but performs no GPU upload headless.
//   * objSetParent (VIBE_Object_SetParent @0x5b0b9c) / objLinkIntoScene
//     (VIBE_Object_LinkIntoScene @0x5b0a20) — NOW REAL (this wave). They bind to the
//     genuine scene_link splice trio (render::SetParent / LinkIntoScene / LinkAsSibling),
//     building a REAL traversable render::SceneNode tree: parent==0 -> LinkIntoScene
//     prepends the node's shadow as the scene-list head (dword_13FD140); parent!=0 ->
//     SetParent links it under the parent's shadow (firstChild / +528-terminated sibling
//     chain). The tree is exposed via ObjectAttachWiringLiveScene() and walked with
//     render::WalkAndInvoke. Each verbatim SceneNode10 gets a SHADOW render::SceneNode
//     carrying the +496/+500/+504/+508/+528/+533 link fields (the native layout cannot
//     keep the original's 4-byte pointer spacing inside the one block — LP64); the +528
//     terminator/dirty byte is synced back into the raw block. The two MAKE-CURRENT
//     leaves (InvalidateCurrent @0x5af2e4 / the LinkIntoScene SetWorldTranslation
//     @0x5af50c) stay inert — they need the live universe (sky-dome mesh / current-cam
//     draw-block invalidation), documented GAP; the LIST SPLICE is full.
//   * objDispose (VIBE_Object_Dispose @0x5b0790) — observer (records the dispose call);
//     the full node teardown / list-unlink is the deferred live-universe target.
//   * SelectLodFrame (VIBE_Mesh_SelectLodFrame @0x5adb6c, a MeshAttachHooks leaf) —
//     needs the LOD-distance globals; left inert (the scan fallback in AttachStockTextures
//     runs instead). initSubMeshEntry stays the faithful raw-byte default (the ObjNode4-
//     typed real InitSubMeshEntry @0x5b0e18 has the incompatible by-value layout).
#include "sim/object_attach_wiring.h"

#include "sim/character_factory.h"      // CharacterFactoryHooks, Set/GetCharacterFactoryHooks
#include "sim/object_lifecycle10.h"     // ObjectAttachToUniverseNode / ObjectInitStruct /
                                        // ObjectAllocDrawData / ObjLife10Hooks / SceneNode10
#include "render/mesh_attach_textures.h"// render::InstallStockTextureAttach + frame:: layout
#include "render/mesh_stock_object.h"   // render::InstallStockObjectRegistry + Registry/LoadAndRegister
#include "render/mesh_lod_name.h"       // render::AttachStockObjectLods / BuildLodFileName /
                                        // MeshLodHooksMut / LodModeByte
#include "render/scene_link.h"          // render::LiveScene / LinkIntoScene / SetParent (REAL splice)
#include "util/matrix.h"                // util::MatrixFromEuler (0x5cb1bc, REAL)

#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace guild::sim {

namespace {

// dword_401010[0..3] — the {0,0,0,0} world-rotation/translation seed the factory
// hands to AttachToUniverseNode (recovered: get_int @0x401010..0x40101c == 0).
const float kAttachZeroSeed[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// CharacterFactoryHooks.attachToUniverseNode adapter -> the GENUINE function.
//   parent = 0 (the factory's hard-zero eax)
//   pos    = parentMat (the factory's edx, threaded as the genuine `pos` vector)
//   xlate  = dword_401010 zero seed (the factory's ebx)
//   ctx    = &model (the factory's edi &v31 lodArg; the genuine fn ignores it)
void* FactoryAttachAdapter(void* parentMat, const char* model) {
    return ObjectAttachToUniverseNode(/*parent=*/nullptr,
                                      /*pos=*/reinterpret_cast<const float*>(parentMat),
                                      /*model=*/model,
                                      /*xlate=*/kAttachZeroSeed,
                                      /*ctx=*/&model);
}

// ===========================================================================
// ObjLife10Hooks leaf adapters — the live chain below AttachToUniverseNode.
// ===========================================================================

// VIBE_Memory_AllocDebug @0x438f10 stand-in: a zeroing heap block. The original is
// a debug heap; the draw block / light block the node allocates live for the node's
// lifetime (the live render path never frees them mid-frame). Leaked deliberately —
// the headless chain has no node teardown wired (objDispose is an observer gap).
void* WireMemAllocDebug(unsigned size, const char* /*tag*/) {
    return std::calloc(size ? size : 1, 1);
}

// VIBE_Object_Spawn(4, name) @0x5b054c — the kind-4 (universe scene-node) path.
//   v4 = AllocDebug(0x21C, "d3:SpawnObject");
//   InitStruct(v4);
//   *(v4+533) = 4 (kind < 5 -> nodeType = kind); LABEL_10: t=4 (no lightinfo branch);
//   *(v4+534) = *(v4+533);  StrNCopyPad(v4, name, 64);  *(v4+520) = off_649D64.
// We allocate the VERBATIM raw-byte SceneNode10 (the live chain's node view; +492
// draw ptr / +76 pos / +132 euler are read at native/verbatim offsets) and run the
// REAL ObjectInitStruct @0x5b0e88 (raw-byte compatible). The active-universe slot
// (+520) is left null headless (no live universe; see GAPS). lightBlock == null
// (kind 4 < 5 allocates no lightinfo block, matching the original).
void* WireObjSpawn(int kind, const char* name) {
    auto* node = new SceneNode10();              // 0x21C + pad, zero-initialised
    ObjectInitStruct(node, /*lightBlock=*/nullptr);          // REAL 0x5b0e88
    node->b(n10::kAttachKind) = static_cast<u8>(kind);       // +533 nodeType = kind(4)
    node->raw[534] = static_cast<u8>(kind);                  // +534 nodeTypeShadow
    if (name) {                                              // StrNCopyPad(node, name, 64)
        std::memset(node->raw, 0, 64);
        std::strncpy(reinterpret_cast<char*>(node->raw), name, 63);
        node->raw[63] = '\0';
    }
    return node;
}

// VIBE_Object_SetPosition @0x5af38c — the record write (a1[19]=x, a1[20]=y, a1[21]=z
// == node+76/+80/+84). The dirty-flag / shadow scene-graph walks the original also
// runs (VIBE_SceneGraph_WalkAndInvoke / TraverseTree) are the live-universe gap; the
// observable record placement IS this write, exactly what object_transform.h reads.
void WireObjSetPosition(SceneNode10* node, const float* pos) {
    if (!node || !pos) return;
    node->f(76) = pos[0];                                    // a1[19]
    node->f(80) = pos[1];                                    // a1[20]
    node->f(84) = pos[2];                                    // a1[21]
}

// VIBE_Object_SetWorldTranslation @0x5af50c — write node+132/+136/+140 (euler) then
// build the +396 frame matrix via the REAL VIBE_Math_MatrixFromEuler @0x5cb1bc. The
// node here is a mesh object (+533 == 4, NOT a camera 3), so the non-negated euler
// path runs (the camera-negate leg is the +533==3 branch). The scene-graph dirty
// walk is the live-universe gap.
void WireObjSetWorldTranslation(SceneNode10* node, const float* xlate) {
    if (!node || !xlate) return;
    node->f(132) = xlate[0];
    node->f(136) = xlate[1];
    node->f(140) = xlate[2];
    util::MatrixFromEuler(reinterpret_cast<const float*>(node->raw + 132),
                          reinterpret_cast<float*>(node->raw + 396));  // +396 frame
}

// ===========================================================================
// LIVE-SCENE splice (rule 13) — the REAL intrusive scene-list splice.
//
// RECONCILIATION (SceneNode10 raw block <-> render::SceneNode). The live chain threads
// the verbatim raw-byte SceneNode10 (the +492 draw ptr / +76 pos / +132 euler live at
// verbatim/native offsets in its 0x21C block). The splice trio (scene_link.cpp) +
// the universe walk (scene_walk.cpp) read the link/flag/type set at:
//     +0x1F0 (496) nextSibling   +0x1F4 (500) prevSibling   +0x1F8 (504) parent
//     +0x1FC (508) firstChild    +0x210 (528) flags528      +0x215 (533) nodeType
// On the 32-bit original those 4-byte-spaced pointer slots live INSIDE the one node
// block. On a 64-bit host the native render::SceneNode cannot preserve that 4-byte
// pointer spacing (its pointers are 8 bytes), so we keep the link fields in a SHADOW
// render::SceneNode (native layout) paired 1:1 with each SceneNode10, and mirror the
// two byte fields the splice/walk also read from the raw block:
//     shadow.flags528 == SceneNode10 +528  (the +528 terminator/dirty byte)
//     shadow.nodeType == SceneNode10 +533  (the +533 node-type byte; kAttachKind)
// The splice reads/writes the SAME logical +496/+500/+504/+508/+528/+533 fields the
// walk reads — only relocated into the shadow so a real 64-bit link survives. The
// shadow IS the node the tree is built from / WalkAndInvoke traverses.
render::LiveScene g_liveScene;
std::unordered_map<SceneNode10*, render::SceneNode*> g_shadow;

render::SceneNode* ShadowFor(SceneNode10* node) {
    if (!node) return nullptr;
    auto it = g_shadow.find(node);
    if (it != g_shadow.end()) return it->second;
    auto* s = new render::SceneNode();              // owns the link fields (leaked; node
                                                    // lifetime == process, like the draw block)
    s->nodeType = node->b(n10::kAttachKind);        // +533 mirror
    s->flags528 = node->b(n10::kFlags528);          // +528 mirror (seed from current byte)
    g_shadow.emplace(node, s);
    return s;
}

// Keep the SceneNode10's +528 byte in sync with its shadow after a splice (so the raw
// block the rest of the chain reads sees the terminator/dirty bits the splice set).
void SyncFlags528(SceneNode10* node, render::SceneNode* shadow) {
    if (node && shadow) node->b(n10::kFlags528) = shadow->flags528;
}

// VIBE_Object_LinkIntoScene @0x5b0a20 — REAL splice. Prepend the spawned node's shadow
// as the scene-list head (+ make-current for a type-3 cam). Builds the live tree.
int g_linkIntoSceneCalls = 0;
void WireObjLinkIntoScene(SceneNode10* node) {
    ++g_linkIntoSceneCalls;
    render::SceneNode* s = ShadowFor(node);
    render::LinkIntoScene(g_liveScene, s);          // REAL 0x5b0a20
    SyncFlags528(node, s);
}

// VIBE_Object_SetParent @0x5b0b9c — REAL splice. parent==0 -> LinkIntoScene; else link
// the child's shadow under the parent's shadow (firstChild / sibling chain).
int g_setParentCalls = 0;
void WireObjSetParent(void* parent, SceneNode10* node) {
    ++g_setParentCalls;
    render::SceneNode* childShadow = ShadowFor(node);
    render::SceneNode* parentShadow =
        ShadowFor(reinterpret_cast<SceneNode10*>(parent));   // null -> LinkIntoScene leg
    render::SetParent(g_liveScene, parentShadow, childShadow);  // REAL 0x5b0b9c
    SyncFlags528(node, childShadow);
}

// VIBE_Object_Dispose @0x5b0790 observer (GAP).
int g_disposeCalls = 0;
void WireObjDispose(SceneNode10* /*node*/) { ++g_disposeCalls; }

// VIBE_Mesh_FindStockObject (no-arg call site) — force the load leg. The genuine
// Mesh_LoadOrFindByName the load leg calls performs the real FindStockObject-or-load
// (and dedups), so returning 0 here is behavior-equivalent to the original's cache probe.
int WireMeshFindStockObject() { return 0; }

// VIBE_Mesh_LoadOrFindByName(name, dir) @0x5d345c — find-or-load the stock object(s).
//   BuildLodFileName(name, dir, v8, 0, v7);  stock = FindStockObject(v7) ?: FindStockObject(v8);
//   if (!stock) { stock = LoadAndRegister(v8, v7);
//                 if (BuildLodFileName(name, dir, _, -1, v7)) LoadAndRegister(v8, v7);
//                 if ((byte_64A098 & 0x7F) == 1) for (i=1;i<3;++i) {
//                     while (!BuildLodFileName(name,dir,v8,i,v7)) if(++i>=3) return stock;
//                     LoadAndRegister(v8, v7); } }
//   return stock.   (1:1 with 0x5d345c; the registry is render::Registry().)
void WireMeshLoadOrFind(const char* name) {
    if (!name) return;
    char v8[256];                                            // base/leaf name
    char v7[256];                                            // dir/second name
    render::StockRegistry& reg = render::Registry();

    render::BuildLodFileName(name, /*secondName=*/name, v8, 0, v7);
    u8* stock = reg.FindStockObject(v7);
    if (!stock) stock = reg.FindStockObject(v8);
    if (stock) return;

    reg.LoadAndRegister(v8, v7);                             // base
    if (render::BuildLodFileName(name, name, v8, -1, v7))    // "_s" variant
        reg.LoadAndRegister(v8, v7);
    if ((render::LodModeByte() & 0x7F) == 1) {               // multi-LOD frames 1..2
        for (int i = 1; i < 3; ++i) {
            while (!render::BuildLodFileName(name, name, v8, i, v7)) {
                if (++i >= 3) return;
            }
            reg.LoadAndRegister(v8, v7);
        }
    }
}

// VIBE_Mesh_AttachStockObjectLods @0x5d1824 — the REAL render-layer attach (it
// reads node+492, allocs the draw block via the allocDrawData hook, and attaches the
// stock LODs via the attachStockTextures hook). The node is the verbatim SceneNode10.
void WireMeshAttachLods(SceneNode10* node, const char* model) {
    render::AttachStockObjectLods(/*object=*/node, /*attachExisting=*/false, model,
                                  /*lodArg=*/0);
}

// MeshLodHooks.allocDrawData — the REAL VIBE_Object_AllocDrawData @0x5b107c (raw-byte,
// allocates node+492 via memAllocDebug). `object` is the verbatim SceneNode10.
void WireAllocDrawData(void* object) {
    ObjectAllocDrawData(reinterpret_cast<SceneNode10*>(object));  // REAL 0x5b107c
}

// MeshAttachHooks.allocPolysAndPoints — the REAL VIBE_Object_AllocPolysAndPoints
// @0x5b0c10, as a raw-byte clone on the relocated per-frame draw block (a2). The
// decompile (1:1): if (a2[2] && a2[3]) { free a2[1]; a2[1]=AllocDebug(40*a2[3]);
// free a2[0]; a2[0]=AllocDebug(80*(a2[2]+8)); } where a2[2]=vertCount, a2[3]=polyCount
// at the shared render::frame offsets, and a2[0]/a2[1] are the native draw-vert /
// draw-poly array ptrs. (a2 == the per-frame block AttachStockTextures fills, NOT the
// ObjNode4-typed SubMeshEntry the by-value reconstruction in object_lifecycle4 uses —
// the two are different views; this raw-byte clone matches the draw block the live
// chain actually threads.) Draw-vert stride 80, draw-poly stride 40 in the original;
// AttachStockTextures lays verts at 80 and polys at 64 (LP64), so size to those.
void WireAllocPolysAndPoints(u8* a2, int /*lodArg*/) {
    if (!a2) return;
    namespace F = render::frame;
    u32 vertCount, polyCount;
    std::memcpy(&vertCount, a2 + F::kVertCount, 4);          // a2[2]
    std::memcpy(&polyCount, a2 + F::kPolyCount, 4);          // a2[3]
    if (vertCount == 0 || polyCount == 0) return;            // if (a2[2] && a2[3])

    // a2[1] = AllocDebug(polyStride * polyCount)  (engine 40*a2[3]; AttachStockTextures
    // draw-poly stride is 64 under LP64, so allocate that to fit the native ptr fields).
    uintptr_t oldPolys = 0, oldPoints = 0;
    std::memcpy(&oldPolys, a2 + F::kPolyArrPtr, sizeof(oldPolys));
    if (oldPolys) std::free(reinterpret_cast<void*>(oldPolys));
    void* polys = std::calloc(static_cast<std::size_t>(polyCount), 64);
    std::memcpy(a2 + F::kPolyArrPtr, &polys, sizeof(void*)); // a2[1]

    // a2[0] = AllocDebug(vertStride * (vertCount + 8))  (engine 80*(a2[2]+8)).
    std::memcpy(&oldPoints, a2 + F::kVertArrPtr, sizeof(oldPoints));
    if (oldPoints) std::free(reinterpret_cast<void*>(oldPoints));
    void* points = std::calloc(static_cast<std::size_t>(vertCount) + 8, 80);
    std::memcpy(a2 + F::kVertArrPtr, &points, sizeof(void*));  // a2[0]
}

} // namespace

// Observers / live-scene access for the e2e: the splice now builds a REAL tree, so
// these expose both the call counts AND the spliced tree (LiveScene + shadows).
int ObjectAttachWiringLinkIntoSceneCalls() { return g_linkIntoSceneCalls; }
int ObjectAttachWiringSetParentCalls() { return g_setParentCalls; }
int ObjectAttachWiringDisposeCalls() { return g_disposeCalls; }

render::LiveScene& ObjectAttachWiringLiveScene() { return g_liveScene; }

render::SceneNode* ObjectAttachWiringShadowFor(SceneNode10* node) {
    auto it = g_shadow.find(node);
    return it == g_shadow.end() ? nullptr : it->second;
}

void ObjectAttachWiringResetLiveScene() {
    for (auto& kv : g_shadow) delete kv.second;
    g_shadow.clear();
    // Re-seat the scene to the boot state: head + currentCamera reset, head == sentinel
    // (the sentinel itself is preserved, re-stamped with its InitStruct +528/+533 seed).
    render::SceneNode* sentinel = g_liveScene.sentinel;
    if (sentinel) {
        sentinel->flags528 = 0x65;
        sentinel->nodeType = 1;
        sentinel->nextSibling = nullptr;
        sentinel->prevSibling = nullptr;
        sentinel->firstChild  = nullptr;
        sentinel->parent      = nullptr;
    }
    g_liveScene.sceneHead     = sentinel;   // dword_13FD140 starts AS the sentinel
    g_liveScene.currentCamera = nullptr;
    g_linkIntoSceneCalls = 0;
    g_setParentCalls = 0;
    g_disposeCalls = 0;
}

void InstallRealObjectAttachWiring() {
    // 1. Compose onto the current factory hooks (keep every other slot; only rebind
    //    the attach leaf to the genuine reconstruction).
    CharacterFactoryHooks h = GetCharacterFactoryHooks();
    h.attachToUniverseNode = &FactoryAttachAdapter;
    SetCharacterFactoryHooks(&h);

    // 2. Bind the ObjLife10Hooks load->attach leaves to their REAL reconstructions
    //    (the texture-upload / scene-list / dispose leaves stay gapped — see header).
    ObjLife10Hooks o{};
    o.memAllocDebug          = &WireMemAllocDebug;            // 0x438f10 (zeroing heap)
    o.objSpawn               = &WireObjSpawn;                 // 0x5b054c (kind-4 path)
    o.objSetParent           = &WireObjSetParent;             // 0x5b0b9c (REAL splice)
    o.objSetPosition         = &WireObjSetPosition;           // 0x5af38c (REAL record write)
    o.objSetWorldTranslation = &WireObjSetWorldTranslation;   // 0x5af50c (REAL + matrix)
    o.objLinkIntoScene       = &WireObjLinkIntoScene;         // 0x5b0a20 (REAL splice)
    o.objDispose             = &WireObjDispose;               // 0x5b0790 (observer GAP)
    o.meshFindStockObject    = &WireMeshFindStockObject;      // force load leg
    o.meshLoadOrFind         = &WireMeshLoadOrFind;           // 0x5d345c (REAL find-or-load)
    o.meshAttachLods         = &WireMeshAttachLods;           // 0x5d1824 (REAL attach)
    o.textureUploadToSurface = nullptr;                       // 0x5db234 GAP (Vulkan runtime)
    o.initSubMeshEntry       = nullptr;                       // faithful raw-byte default
    ObjLife10SetHooks(o);

    // 2b. Seed the LiveScene exactly as the engine's VIBE_Render_InitEngineDevice
    //     @0x5af984 boot does (5afc21..5afc40):
    //       InitStruct(dword_13FCF4C);          // the sentinel node is itself InitStruct'd
    //       dword_13FCF10 = dword_13FD140 = dword_13FCF4C;  // head + childHead START as it
    //       dword_13FCD1C = 0;                              // no camera current yet
    //     InitStruct (0x5b0e88) sets +528 = (..&0x9A)|0x65 -> bit0 SET, so the sentinel is
    //     a permanent chain terminator (the walk's flags528&1 stop test). The scene head
    //     STARTS as the sentinel, so the first LinkIntoScene back-chains the sentinel's
    //     +496 to the first real node — and the walk descends from sentinel->nextSibling.
    //     activeUniverse / the make-current leaves (InvalidateCurrent @0x5af2e4 /
    //     SetWorldTranslation @0x5af50c) stay inert headless (live-universe GAP).
    static render::SceneNode s_sentinel;
    s_sentinel.flags528 = 0x65;                  // == InitStruct's +528 seed (bit0 set)
    s_sentinel.nodeType = 1;                      // InitStruct sets +533 = 1
    g_liveScene.sentinel  = &s_sentinel;
    g_liveScene.sceneHead = &s_sentinel;          // dword_13FD140 starts AS the sentinel
    g_liveScene.currentCamera = nullptr;          // dword_13FCD1C = 0

    // 3. Bind the render-layer attach hooks:
    //    AttachStockObjectLods.allocDrawData       -> REAL Object_AllocDrawData @0x5b107c
    //    AttachStockObjectLods.attachStockTextures -> REAL AttachStockTextures @0x5d1114
    //    AttachStockTextures.findStockObject       -> REAL Registry::FindStockObject @0x5d10d0
    //    AttachStockTextures.allocPolysAndPoints   -> REAL AllocPolysAndPoints @0x5b0c10
    render::MeshLodHooksMut().allocDrawData = &WireAllocDrawData;
    render::InstallStockTextureAttach();
    render::InstallStockObjectRegistry();
    render::MeshAttachHooksMut().allocPolysAndPoints = &WireAllocPolysAndPoints;
}

} // namespace guild::sim
