#pragma once
// ===========================================================================
// object_lifecycle8.{h,cpp} — VIBE_Object_* remaining leaves (batch 8).
// namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family after batches
// 1..7. This batch is the script-command "attach animation / flight / light"
// entry points plus three larger scene-graph leaves the brief flagged:
//
//   * The CmdAttachAnimation* family (Once / Looped / LoopedVerified / ToDummy)
//     — each copies a (UTF-16-ish, 2-byte-stepped) name out of a script value
//     cell into a stack buffer, appends ".baf", strips path+extension, then (if
//     a valid model node) loads the stream and attaches it to the model's bone,
//     packing a 4-byte animation flag word with verbatim bit ops. The re-entrant
//     "command-pump" head (dword_62E8CC chain) is preserved exactly.
//   * CmdObjectFlight / CmdObjectFlightSingle — script flight markers: resolve
//     object handles via FindByHandle and either draw 3D markers or apply a bone
//     transform; preserve the same dword_62E8A8/dword_62E8CC pump dance.
//   * CmdAttachLightAtDummy — resolve a dummy bone's world position, load a light
//     object group there, build its cache.
//   * AssignToRoomByName 0x5775ac — name-prefix + within-tolerance room assign.
//   * ParseAndAttachAvatar 0x4fffd4 — reparse a node name, create the gameplay
//     Gebaeude record for a building-kind node and cross-link it.
//   * Dispose 0x5b0790 — the full scene-object teardown (universe slot switch,
//     draw/anim/emitter/light/shadow frees, recursive child + sibling dispose).
//   * ComputeScreenBounds 0x5b5c70 — billboard / mesh on-screen AABB projection.
//   * SelectTextureSet 0x5b3f54 — swap a mesh's texture set, reloading textures.
//
// Reuses real reconstructed siblings (NOT mocks):
//   * util::StrCmpNoCaseN  (string_ops.cpp @0x5e0db0)  — AssignToRoomByName
//   * util::VectorWithinTolerance (math.cpp @0x5caa4c)  — AssignToRoomByName
//   * util::StrCmpNoCase   (string_ops.cpp @0x5cb8f0)  — ParseAndAttachAvatar
//   * util::StrChrLast     (string_ops.cpp @0x5d3ef0)  — ParseAndAttachAvatar
//   * g_activeUniverse (off_649D64, owned by character_query.cpp) — Dispose root.
//
// All unreconstructed cross-module leaves (Anim_*, Light_*, Texture_*, Mesh_*,
// Transform_*, Render_*, Sound3d_*, Shadow_*, Scene_*, Universe_*, Memory_*,
// Building_CreateGebaeude, Object_ParseNameAndBind, the script error reporter,
// the dummy-validity predicate) are routed through ObjLife8Hooks with INERT
// default implementations defined in THIS library .cpp. Tests install their own
// captor hooks; nothing in a test defines a symbol that src/ references.
//
// LP64 caveat: the originals store node/handle pointers in 32-bit slots. We keep
// the touched pointer slots as named POINTER members (void*/SceneNode8*) so a
// real 64-bit pointer survives; only genuine i32 fields stay i32.
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// SceneNode8 — flat byte view of the "d3:SpawnObject" scene-graph node, for the
// fields THIS batch touches. Same opaque ~0x21C block as SceneNode6/7/
// SceneObject; re-declared here (named only where this batch touches) to avoid
// an ODR clash with the sibling translation units' views. Touched byte offsets:
//   +0x00  (0)    name             (2-byte-stepped script name; ToDummy reads +64)
//   +0x4C  (76)   pos[3]           local position (bone-chain source)
//   +0x48  (72)   parseKindWord    (a1+72 in ParseAndAttachAvatar; -1 == none)
//   +0xC0  (192)  toDummyFlags     (a1+192 ToDummy reads .192 / .196)
//   +0xC4  (196)  toDummyAux
//   +0x1C8 (456)  (light cache anchor; not modeled)
//   +0x1C4 (452)  model            (a1+460 model/mesh root)  -- see kModel
//   +0x1D4 (468)  drawBlock        (a1+468)  freed in Dispose
//   +0x1E8 (488)  emitterBlock     (a1+488)  freed in Dispose
//   +0x1EC (492)  meshBlock        (a1+492)  texture-set / dispose
//   +0x1F0 (496)  altChild         (a1+496)
//   +0x1F8 (504)  parent           (a1+504)
//   +0x1FC (508)  firstChild       (a1+508)
//   +0x200 (512)  ownerLink        (a1+512)  building back-ptr
//   +0x208 (520)  universe         (a1+520)  owning universe ptr
//   +0x210 (528)  flags528         bit2 = dirty
//   +0x211 (529)  flags529         bit2 = has shadow casters
//   +0x215 (533)  attachKind       (3=world, 1/5/6/8 = light/cache kinds)
//   +0x216 (534)  attachKindShadow
//   +0x68  (104)  billboardRadius  (ComputeScreenBounds)
// ===========================================================================
struct SceneNode8 {
    u8 raw[0x21C];

    float& f(int off) { return *reinterpret_cast<float*>(raw + off); }
    i32&   d(int off) { return *reinterpret_cast<i32*>(raw + off); }
    u8&    b(int off) { return raw[off]; }
    char*  s(int off) { return reinterpret_cast<char*>(raw + off); }

    void reset() { for (auto& x : raw) x = 0; }
    SceneNode8() { reset(); }
};

// ===========================================================================
// AnimAttachFlags — the 4-byte animation flag word the CmdAttachAnimation*
// family packs and hands to Anim_AttachToBone. Modeled as a union so a test can
// read back the exact byte layout the original built (BYTE0..BYTE3 of v18/v20/
// v27/v30). The seed constants are baked verbatim per command.
// ===========================================================================
union AnimAttachFlags {
    u32 word;
    u8  byte[4];
};

// ===========================================================================
// Mockable hooks for the unreconstructed leaves. nullptr field => inert default.
// ===========================================================================
struct ObjLife8Hooks {
    // --- Animation stream / attach leaves (CmdAttachAnimation* family) -------
    // VIBE_Anim_FindFreeMeshSlot() -> non-zero if a free stream slot exists.
    // When 0, the command first loads the stream. Default: 0 (forces a load).
    int (*animFindFreeMeshSlot)() = nullptr;
    // VIBE_Anim_LoadStreamToStock(name, 0) — preload a .baf stream by name.
    void (*animLoadStreamToStock)(const char* name) = nullptr;
    // VIBE_Light_SetGrayColorThunk(0, 4, &flagWordOut) — the originals call this
    // immediately before writing the flag word (a UI/colour side effect); it does
    // NOT seed the word. Default: noop.
    void (*lightSetGrayColorThunk)(AnimAttachFlags* out) = nullptr;
    // VIBE_Anim_AttachToBone(model, flagWord) -> attachment block ptr (0 on fail).
    void* (*animAttachToBone)(void* model, u32 flagWord) = nullptr;
    // VIBE_Anim_PruneExpiredAttachments(model). Default: noop.
    void (*animPruneExpiredAttachments)(void* model) = nullptr;
    // VIBE_Script_ReportError(msg) — diagnostic path (verified attach failed,
    // ObjectFlight could not find object, ...). Default: noop.
    void (*reportError)(const char* msg) = nullptr;
    // The dummy-validity predicate (loc_5CB930): returns non-zero when the dummy
    // arg is a valid object to attach to. Default: returns 1 (proceed).
    int (*dummyValid)(void* dummy) = nullptr;

    // --- Flight markers (CmdObjectFlight / Single) --------------------------
    // VIBE_Object_FindByHandle(0, kind, id, 0, ctx) -> node ptr (0 if missing).
    void* (*findByHandle)(int kind, int id, void* ctx) = nullptr;
    // VIBE_Render_DrawObjectMarkers3D(self, scaledColor, nodes[5], foundCount).
    void (*drawObjectMarkers3D)(void* self, int scaledColor, void* const nodes[5],
                                int foundCount) = nullptr;
    // VIBE_Character_ApplyBoneTransform(node, scaledVal).
    void (*characterApplyBoneTransform)(void* node, int scaledVal) = nullptr;

    // --- CmdAttachLightAtDummy ----------------------------------------------
    // VIBE_Transform_PointThroughBoneChain(mtx, posIn, posOut[3]). Default copies.
    void (*transformPointThroughBoneChain)(const float* mtx, const float* posIn,
                                           float* posOut) = nullptr;
    // VIBE_Scene_LoadObjectGroup(name, 0, kind, 0) -> node ptr. Default: 0.
    void* (*sceneLoadObjectGroup)() = nullptr;
    // VIBE_Object_SetPosition(node, pos[3]).
    void (*objectSetPosition)(void* node, const float* pos) = nullptr;
    // VIBE_Light_AttachAtFrameMatrix(dummy). Default: noop.
    void (*lightAttachAtFrameMatrix)(void* dummy) = nullptr;
    // VIBE_Object_SetWorldTranslation(node, scratch). Default: noop.
    void (*objectSetWorldTranslation)(void* node) = nullptr;
    // VIBE_Light_RequestObjectCache(node). Default: noop.
    void (*lightRequestObjectCache)(void* node) = nullptr;

    // --- ParseAndAttachAvatar ------------------------------------------------
    // VIBE_Object_ParseNameAndBind(node, ctx) -> non-zero on success. Default: 1.
    int (*parseNameAndBind)(SceneNode8* node, void* ctx) = nullptr;
    // VIBE_Building_CreateGebaeude(kind, 0xFFFF) -> Gebaeude record ptr (or 0).
    void* (*buildingCreateGebaeude)(unsigned kind) = nullptr;

    // --- Dispose -------------------------------------------------------------
    // VIBE_Universe_SwitchActiveSlot(slot, 1) -> char. Default: 0.
    char (*universeSwitchActiveSlot)(int slot) = nullptr;
    // VIBE_Memory_FreeDebug(ptr) — free a tracked block. Default: noop.
    void (*memFreeDebug)(void* ptr) = nullptr;
    // VIBE_Render_FreeObjectNode(renderNode, self). Default: noop.
    void (*renderFreeObjectNode)(void* renderNode, SceneNode8* self) = nullptr;
    // VIBE_Anim_FreeObjAnimData(self). Default: noop.
    void (*animFreeObjAnimData)(SceneNode8* self) = nullptr;
    // VIBE_Object_UnlinkFromScene(self). Default: noop.
    void (*objectUnlinkFromScene)(SceneNode8* self) = nullptr;
    // VIBE_Light_RemoveCacheEntry / SceneGraph walk for a node. Default: noop.
    void (*lightRemoveCacheEntry)(SceneNode8* self) = nullptr;
    // VIBE_Shadow_ClearAllCasters(self). Default: noop.
    void (*shadowClearAllCasters)(SceneNode8* self) = nullptr;
    // VIBE_Sound3d_DetachIfValid(handle). Default: noop.
    void (*sound3dDetachIfValid)(i32 handle) = nullptr;
    // VIBE_Object_FreeDrawData(self). Default: noop.
    void (*objectFreeDrawData)(SceneNode8* self) = nullptr;

    // --- ComputeScreenBounds / SelectTextureSet -----------------------------
    // VIBE_Transform_PointToBoneLocalSpace(node, viewMtx, out[4], localPos) — for
    // the billboard branch. Default: copies localPos into out (w=1).
    void (*transformPointToBoneLocalSpace)(SceneNode8* node, const float* viewMtx,
                                           float* out, const float* localPos) = nullptr;
    // VIBE_Coord_ConvertX() — view->screen scalar conversion side effect. noop.
    void (*coordConvertX)() = nullptr;
    // VIBE_Texture leaves for SelectTextureSet are routed through a single
    // "applyTextureSwap" so the record/loop logic stays faithful and testable.
    // Returns non-zero when the swap succeeded for this poly group.
    int (*applyTextureSwap)(void* node, int groupIndex, u8 set) = nullptr;
};
void ObjLife8SetHooks(const ObjLife8Hooks& hooks);
void ObjLife8ResetHooks();

// ===========================================================================
// Re-entrant command-pump shared state (dword_62E8A8 head + dword_62E8CC link).
// The CmdAttach* / CmdObjectFlight* handlers run twice: once as the "scan"
// pass (dword_62E8CC's +44 == this handler) and once as the "apply" pass. We
// model the two globals as a small POD the test owns so the exact branch is
// reproducible without touching real script-VM globals.
// ===========================================================================
struct CmdPumpState {
    // dword_62E8CC: when non-null AND its +44 entry == the running handler, the
    // handler takes the "scan" branch. Modeled as a flag + the matching handler id.
    bool   scanPassActive = false;   // dword_62E8CC != 0
    int    scanHandlerId  = 0;       // *(dword_62E8CC + 44) handler id
    // dword_62E8A8 + 2564 == 1 gates the apply-pass re-queue; +2528 is the slot
    // the handler stamps itself into. Modeled as a flag + a captured id.
    bool   applyRequeueGate = false; // *(dword_62E8A8 + 2564) == 1
    int    requeuedHandlerId = 0;    // the last id written to +2528
    bool   requeued = false;
    // CmdObjectFlight* scan pass re-queues iff dword_62D4E4 || dword_62D4E8.
    bool   flightPending = false;
};

// Handler ids for the pump (arbitrary stable tags; match what +44 would hold).
enum CmdHandlerId {
    kCmdAttachAnimationLooped     = 1,
    kCmdAttachAnimLoopedVerified  = 2,
    kCmdObjectFlight              = 3,
    kCmdObjectFlightSingle        = 4,
};

// ===========================================================================
// Public faithful entry points.
// ===========================================================================

// 0x43ec7c — VIBE_Object_CmdAttachAnimationOnce. Copy name from `nameCell`,
// append ".baf", strip path+ext, and (if the model exists) load+attach with a
// one-shot flag word (BYTE0 of v18 = 0x00, the rest from Light_SetGrayColorThunk;
// the original seeds v18[0]=0x25000 == 151552). Returns 0. `outFlags` (optional)
// receives the packed flag word.
int ObjectCmdAttachAnimationOnce(SceneNode8* self, const char* nameCell,
                                 AnimAttachFlags* outFlags = nullptr);

// 0x43eab8 — VIBE_Object_CmdAttachAnimationLooped. As Once, but loops; the flag
// word's BYTE1 bit3 follows `loopFlagZero` (cell==0 -> set bit, else clear),
// BYTE2 |= 2, BYTE0 = 0, BYTE1 = (BYTE1 & 0x2F) | 0x50. Runs the pump twice.
int ObjectCmdAttachAnimationLooped(SceneNode8* self, const char* nameCell,
                                   int loopCellValue, CmdPumpState* pump,
                                   AnimAttachFlags* outFlags = nullptr);

// 0x43edcc — VIBE_Object_CmdAttachAnimLoopedVerified. As Looped, but seeds
// v27=0x20000, BYTE1 bit4 from (cell==1), BYTE0 = cell low byte, BYTE1 =
// (BYTE1 & 0x3F) | 0x40; on attach failure reports the diagnostic.
int ObjectCmdAttachAnimLoopedVerified(SceneNode8* self, const char* nameCell,
                                      int verifyCellValue, CmdPumpState* pump,
                                      AnimAttachFlags* outFlags = nullptr);

// 0x43eff8 — VIBE_Object_CmdAttachAnimationToDummy. Validate `dummy`, copy the
// SELF node's name (+64), append/strip, then attach with a flag word seeded from
// the dummy's +192 (mode) / +196 (random-range) fields. Returns 1.
char ObjectCmdAttachAnimationToDummy(SceneNode8* self, SceneNode8* dummy,
                                     AnimAttachFlags* outFlags = nullptr);

// 0x43f7b8 — VIBE_Object_CmdObjectFlightSingle. Pump-aware: resolve one handle
// and apply a bone transform scaled by selfVal/17. Returns 0 on the scan pass,
// 1 on the apply pass. `selfVal` is *a1, `targetId` is *a2.
int ObjectCmdObjectFlightSingle(int selfVal, int targetId, void* self,
                                CmdPumpState* pump);

// 0x43f678 — VIBE_Object_CmdObjectFlight. Pump-aware: resolve up to five handles,
// draw 3D markers for the found ones (color = colorVal/14). Returns 1 on the
// apply pass (0 if `selfHandle` is null -> reports error, or on the scan pass).
int ObjectCmdObjectFlight(void* selfHandle, int colorVal, const int ids[5],
                          void* self, CmdPumpState* pump);

// 0x43e6bc — VIBE_Object_CmdAttachLightAtDummy. Resolve the dummy's world pos via
// the bone chain, load a light object group there, set its world translation and
// request its cache. Returns the loaded node ptr (0 if the dummy is null).
void* ObjectCmdAttachLightAtDummy(SceneNode8* dummy);

// 0x5775ac — VIBE_Object_AssignToRoomByName. If neither the node's name nor its
// name+1 prefix-matches `roomName`, return 1 (no match). Else compute the node's
// world position via the bone chain and, if within 100.0 of the room anchor,
// store the node into the room record (+80) and return 0; else return 1.
// `roomAnchor` is the room's anchor pos (a2+64); `roomOut` receives the assignment.
char ObjectAssignToRoomByName(SceneNode8* node, const char* roomName,
                              const float roomAnchor[3], SceneNode8** roomOut);

// 0x4fffd4 — VIBE_Object_ParseAndAttachAvatar. Re-parse+bind the node name; for a
// building-kind node (parseKindWord high byte == 2) create the Gebaeude record
// (kind 71 special-cases an avatar table lookup) and cross-link it (+512 <-> +97).
// For a non-building node, inherit the parent's +512. Returns the parse result.
// `avatarTable` is the 4-row x 756-byte avatar name table (byte_13CD6A0; null =>
// the kind-71 path falls straight through). `avatarTableLoaded` == byte_13CD994.
int ObjectParseAndAttachAvatar(SceneNode8* node, void* ctx,
                               const char* avatarTable, int avatarTableRows);

// 0x5b0790 — VIBE_Object_Dispose. Full scene-object teardown. If the node's
// universe (+520) differs from g_activeUniverse, switch to it (recording the
// slot to restore). Free draw/anim/emitter/light/shadow data, unlink from the
// render-node list (entries whose +184 owner == self), recursively dispose the
// first child (+508) and the alt child (+496, unless its +528 bit0 set), detach
// sound, free draw data, free the node block, restore the universe slot.
// `renderHead`/`renderSentinel` model the *(universe+164) render list.
struct DisposeRenderNode {
    SceneNode8* owner = nullptr;        // +184 (v7[184])
    DisposeRenderNode* next = nullptr;  // +194 (v7[194])
};
char ObjectDispose(SceneNode8* node, void* nodeUniverse,
                   DisposeRenderNode* renderHead, DisposeRenderNode* renderSentinel,
                   int universeSlot);

// 0x5b5c70 — VIBE_Object_ComputeScreenBounds. When attachKind(+533)==0 (billboard),
// project the local pos through the view matrix, expand by billboardRadius(+104),
// and write the screen AABB into out[0..3] (left, top, right, bottom). Returns 1.
// The mesh branch (attachKind!=0) bottoms out in unreconstructed mesh/transform
// leaves and is routed through hooks (and the screen clip from the supplied
// `screenClip` = {minX,minY,maxX+1,maxY+1}); the billboard math core is exact.
struct ScreenClip {
    int minX = 0, minY = 0, maxXp1 = 0, maxYp1 = 0;  // dword_13ECE58/5C/60/64
};
// Recovered projection constants (gilde.exe; see .cpp for addresses):
//   flt_13FCD0C (xScale), flt_13FCAF8 (yScale), flt_13FCD18 (xOffset),
//   flt_13FCD10 (yOffset), flt_628530 (radiusScale).
struct ScreenProj {
    float xScale  = 1.0f;   // flt_13FCD0C
    float yScale  = 1.0f;   // flt_13FCAF8
    float xOffset = 0.0f;   // flt_13FCD18
    float yOffset = 0.0f;   // flt_13FCD10
    float radiusScale = 1.0f; // flt_628530
};
char ObjectComputeScreenBoundsBillboard(SceneNode8* node, const float* viewMtx,
                                        const ScreenProj& proj, int out[4]);

// 0x5b3f54 — VIBE_Object_SelectTextureSet. Swap a mesh's active texture set to
// `set`. Returns 1 (no-op success) when `set` already active; 0 when the mesh is
// missing or `set` out of range; else iterates the poly groups calling the
// applyTextureSwap hook and returns 1 on overall success (0 if any new texture
// could not be found). `polyGroupCount` models *(mesh+480), `setCount` *(mesh+484).
char ObjectSelectTextureSet(SceneNode8* node, u8 set, int polyGroupCount,
                            int setCount, u8 currentSet);

}  // namespace guild::sim
