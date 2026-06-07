#pragma once
// ===========================================================================
// object_lifecycle5.{h,cpp} — VIBE_Object_* scene-graph GEOMETRY / TRANSFORM /
// transparency / suspend-state batch (batch 5). namespace guild::sim.
// ===========================================================================
// MODULE: the next untranslated slice of the VIBE_Object_* family that operates
// on the heap scene-GRAPH node (the 0x21C-byte "d3:SpawnObject" block already
// modeled as SceneNode3 in object_lifecycle3.h / SceneObject in
// object_lifecycle2.h). batches 1..4 covered the gameplay record, the simple
// flag/transform setters, the find/door/type classifiers and the alloc/link
// teardown leaves. This batch covers the geometry + bone-transform + material
// transparency + suspend/hidden-state group:
//
//   * VIBE_Object_ComputeBoundingRadius  (0x5b2cf8) — bounding sphere over the
//     8 bbox corners (or the per-node fixed radius).
//   * VIBE_Object_AttachToBone           (0x5b2b70) — copy a bone name into the
//     node, validate it against the skeleton's bone table, rebuild bone matrices.
//   * VIBE_Object_TransformPointToParent (0x5b7d14) — world<->parent transform of
//     a point + euler triple through the bone-world matrix.
//   * VIBE_Object_ApplyParentTransform   (0x5b7e24) — thunk: transform then set
//     position + world translation.
//   * VIBE_Object_ReparentWithTransform  (0x5b7e54) — re-link a node under a new
//     parent preserving its world transform (+ its child sub-nodes).
//   * VIBE_Object_AssignMeshData         (0x5b2c70) — bind the selected LOD mesh
//     into +460, recompute bone world matrix, mark dirty.
//   * VIBE_Object_RebindParentMesh       (0x5b4420) — dispose old mesh/draw data,
//     load a new mesh by name, re-attach + rebuild the light cache.
//   * VIBE_Object_ChangeTransparency     (0x5b2710) — palette-swap a submesh's
//     textures to/from a transparent clone set.
//   * VIBE_Object_ChangeTransparencySubMeshes (0x5b2964) — drive ChangeTransparency
//     across each submesh of a node.
//   * VIBE_Object_ApplyTransparencyTree  (0x5b29b0) — walk the subtree applying
//     ChangeTransparencySubMeshes.
//   * VIBE_Object_ToggleSuspendStateNamed (0x5b4274) / RestoreSuspendState
//     (0x5b433c) — push/restore the nodeType-suspend (state 1) with the '!' name
//     copy + light refresh.
//   * VIBE_Object_DetachAndRelease       (0x5b4258) — unlink + dispose.
//   * VIBE_Object_SetActiveCamera        (0x5b0bf0) — set the active-camera global.
//
// Unreconstructed cross-module leaves (Mesh / Transform / Math / Light / Texture
// / Anim / Memory / SceneGraph) AND sibling VIBE_Object_* leaves that live in
// other library .cpp files under FILE-STATIC (anonymous-namespace) names — and
// are therefore not linkable across translation units — are routed through
// ObjLife5Hooks (inert defaults installed by THIS .cpp). The faithful node-field
// arithmetic and control flow run regardless; tests install captor hooks to
// assert ordering / arguments and to feed mesh-bbox data.
//
// The node block is the SAME layout as SceneNode3; to avoid colliding with that
// translation unit's view we re-declare an identical Node5 view here.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x5b2cf8  VIBE_Object_ComputeBoundingRadius
//   0x5b2b70  VIBE_Object_AttachToBone
//   0x5b7d14  VIBE_Object_TransformPointToParent
//   0x5b7e24  VIBE_Object_ApplyParentTransform
//   0x5b7e54  VIBE_Object_ReparentWithTransform
//   0x5b2c70  VIBE_Object_AssignMeshData
//   0x5b4420  VIBE_Object_RebindParentMesh
//   0x5b2710  VIBE_Object_ChangeTransparency
//   0x5b2964  VIBE_Object_ChangeTransparencySubMeshes
//   0x5b29b0  VIBE_Object_ApplyTransparencyTree
//   0x5b4274  VIBE_Object_ToggleSuspendStateNamed
//   0x5b433c  VIBE_Object_RestoreSuspendState
//   0x5b4258  VIBE_Object_DetachAndRelease
//   0x5b0bf0  VIBE_Object_SetActiveCamera
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Node5 — flat byte/dword/float view of the "d3:SpawnObject" node. Identical
// layout to SceneNode3; named only on the fields this batch touches.
//   +0x000 (0)   name[0]    — first char (33='!' suspended-name prefix,
//                              114='r' room)
//   +0x04C (76)  pos[3]     (a1[19..21]) — bone-local transform origin
//   +0x068 (104) fixedRadius (a1[26])    — per-node radius when flag533==0 path
//   +0x215 (533) byte533    — nodeType / suspend-state (1=suspended)
//   +0x216 (534) byte534    — saved nodeType during suspend
//   +0x1CC (460) meshData   (a1[115])    — bound LOD mesh record
//   +0x1EC (492) skeleton   (a1[123])    — skeleton/draw record (+180 boneName,
//                                          +260 boneTable)
//   +0x1F8 (504) parentBone (a1[126])    — parent node carrying the bone owner
//   +0x1F0 (496) selfLink   (a1[124])
//   +0x1FC (508) attachedChild (a1[127]) — sub-object created by RebindParentMesh
//   +0x210 (528) flags528   — bit2 dirty, bit7(0x80) negative-scale marker
//   +0x211 (529) flags529   — bit7(0x80) transparency-active marker
//   +0x212 (530) flags530, +0x213 (531) flags531
//   +0x1E8 (488) boneArray  (a1[122])    — bone-chain array (stride 56) used by
//                                          ReparentWithTransform
// ===========================================================================
struct Node5 {
    u8 raw[0x21C];   // 540 bytes — scalar (byte/float/int) fields by raw offset.

    // The original is 32-bit: the node link fields at +460/+488/+492/+496/+504/
    // +508 are 4-byte dwords holding pointers, and several are only 4 bytes
    // apart. A native 64-bit pointer is 8 bytes, so storing them inside `raw`
    // at the original offsets would make adjacent links OVERLAP (e.g. +492 and
    // +496, or +488 and +496). Since each of these offsets is semantically a
    // pointer-ONLY field (never read as a scalar) and each scalar field is never
    // read as a pointer, we keep the link fields as SEPARATE native-pointer
    // members and route p(off) to the right one. f()/d()/b()/sb() always read
    // the raw block (the scalar view) at the faithful offset.
    void* link460 = nullptr;  // +460 meshData
    void* link488 = nullptr;  // +488 boneArray
    void* link492 = nullptr;  // +492 skeleton
    void* link496 = nullptr;  // +496 selfLink / first-child link
    void* link504 = nullptr;  // +504 parentBone
    void* link508 = nullptr;  // +508 attachedChild / parent-chain head

    float& f(int byteOff) { return *reinterpret_cast<float*>(raw + byteOff); }
    i32&   d(int byteOff) { return *reinterpret_cast<i32*>(raw + byteOff); }
    u8&    b(int byteOff) { return raw[byteOff]; }
    i8&    sb(int byteOff) { return reinterpret_cast<i8*>(raw)[byteOff]; }
    char*  str(int byteOff) { return reinterpret_cast<char*>(raw + byteOff); }
    void*& p(int byteOff) {
        switch (byteOff) {
            case 460: return link460;
            case 488: return link488;
            case 492: return link492;
            case 496: return link496;
            case 504: return link504;
            case 508: return link508;
        }
        // Any other offset is a programming error in this module.
        return link460;
    }

    void reset() {
        for (auto& x : raw) x = 0;
        link460 = link488 = link492 = link496 = link504 = link508 = nullptr;
    }
    Node5() { reset(); }
};

// flt_6282F4 — the 0.125 bbox-centroid averaging constant used by
// ComputeBoundingRadius (1/8 of the 8 corner sum). Recovered rodata.
extern const float kBBoxCentroidScale;  // flt_6282F4

// dword_62EB38 frame-stamp not used by this batch (ToggleHiddenState lives in
// batch 3); dword_13FCD1C (active camera) is modeled module-locally below to
// avoid an ODR clash with the per-file views in batches 3/4.

// ===========================================================================
// Mockable hooks for the unreconstructed leaves the originals call. nullptr =>
// inert. The node-field arithmetic still runs faithfully. Mesh-bbox data is fed
// through computeBoundingVolume so ComputeBoundingRadius is testable in process.
// ===========================================================================
struct ObjLife5Hooks {
    // --- ComputeBoundingRadius ---
    // VIBE_Transform_PointToBoneLocalSpace(node, point, outVec3, node+76)
    void (*pointToBoneLocalSpace)(Node5* node, const float* point,
                                  float* out, const float* origin) = nullptr;
    // VIBE_Mesh_TransformBoundingVolume(node, point, 0) — transform the mesh
    // bbox; followed by *(meshFn+500)() returning the 8-corner array (stride 20
    // floats). The captor returns a pointer to 8*20 floats (corner i at [i*20]),
    // or nullptr to mean "no bbox" (original returns 0 -> radius unchanged).
    const float* (*meshBoundingCorners)(Node5* node, const float* point) = nullptr;

    // --- AttachToBone ---
    // VIBE_Anim_AssignSubMeshBones(parentNode) ; VIBE_Anim_ComputeBoneMatrices.
    void (*animAssignSubMeshBones)(Node5* parentNode) = nullptr;
    void (*animComputeBoneMatrices)(Node5* parentNode) = nullptr;

    // --- TransformPointToParent ---
    // VIBE_Transform_ComputeBoneWorldMatrix(node, 0, which) ; which 0 then 1.
    void (*computeBoneWorldMatrix)(Node5* node, int a, int which) = nullptr;
    // VIBE_Math_MatrixInverse(srcMat16, dstMat16).
    void (*matrixInverse)(const float* src, float* dst) = nullptr;
    // VIBE_Math_MatrixCopy(dst16, src16).
    void (*matrixCopy)(float* dst, const float* src) = nullptr;
    // VIBE_Math_MatrixTransformVectors(angles3, mat16, outMat16).
    void (*matrixTransformVectors)(const float* angles, const float* mat,
                                   float* out) = nullptr;
    // VIBE_Math_MatrixToEuler(angles3) — in place.
    void (*matrixToEuler)(float* angles) = nullptr;
    // The world matrix produced by computeBoneWorldMatrix is read back into a
    // local 4x4. The captor fills a 16-float matrix; default = identity.
    void (*fetchWorldMatrix)(Node5* node, float* outMat16) = nullptr;

    // --- ReparentWithTransform ---
    // VIBE_Transform_PointThroughBoneChain(node, point3, out3).
    void (*pointThroughBoneChain)(Node5* node, const float* point,
                                  float* out) = nullptr;
    // sibling Object_* leaves (file-static in other TUs): route through hooks.
    void (*objSetPosition)(Node5* node, const float* pos) = nullptr;
    void (*objSetWorldTranslation)(Node5* node, const float* angles) = nullptr;
    void (*objUnlinkFromList)(Node5* node) = nullptr;
    void (*objSetParent)(Node5* parent, Node5* node) = nullptr;
    void (*objLinkIntoScene)(Node5* node) = nullptr;
    // VIBE_Object_FindByHandle(node, 511, 0, target, extra) — returns truthy if a
    // matching ancestor handle is found (we model "found"); default => 0.
    int  (*objFindByHandle)(Node5* node, int mask, int z, Node5* target,
                            int extra) = nullptr;

    // --- AssignMeshData ---
    // VIBE_Mesh_SelectLodFrame(node) -> mesh record (or 0). default 0.
    Node5* (*meshSelectLodFrame)(Node5* node) = nullptr;
    // *(*(meshRecord+16)+496)() — finalize-mesh virtual call. default noop.
    void (*meshFinalize)(Node5* node, Node5* meshRecord) = nullptr;

    // --- RebindParentMesh ---
    void (*objDispose)(Node5* node) = nullptr;
    void (*objFreeDrawData)(Node5* node, int arg) = nullptr;
    Node5* (*sceneGraphGetFirstActiveChild)(Node5* node) = nullptr;
    void (*meshLoadOrFindByName)(const char* name) = nullptr;
    void (*meshAttachStockObjectLods)(Node5* node, const char* name,
                                      int arg) = nullptr;
    void (*textureUploadAllRecords)() = nullptr;
    void (*lightBuildObjectCache)(Node5* node) = nullptr;

    // --- ChangeTransparency ---
    void* (*memAlloc)(int size, const char* tag) = nullptr;   // VIBE_Memory_AllocDebug
    void  (*memFree)(void* p) = nullptr;                      // VIBE_Memory_FreeDebug
    // VIBE_Texture_CloneIfPaletteMatch(tex, color, 1) -> swapped tex.
    int   (*textureCloneIfPaletteMatch)(int tex, int color) = nullptr;
    // VIBE_Texture_DetachClone(tex, 1) -> original tex.
    int   (*textureDetachClone)(int tex) = nullptr;
    // VIBE_Light_ApplyVertexShading(node, submesh) -> u8.
    u8    (*lightApplyVertexShading)(Node5* node, void* submesh) = nullptr;

    // --- suspend-state ---
    // VIBE_Light_RefreshAllObjects(0).
    void (*lightRefreshAllObjects)() = nullptr;

    // --- SetActiveCamera ---
    // VIBE_Object_InvalidateCurrent(0) — dirty the formerly-current node.
    void (*objInvalidateCurrent)(u8 arg) = nullptr;
};
void ObjLife5SetHooks(const ObjLife5Hooks& hooks);
void ObjLife5ResetHooks();

// The active-camera global (gilde.exe dword_13FCD1C). Module-local view; set via
// the accessor below for SetActiveCamera tests.
extern Node5* g_activeCamera5;

// ---------------------------------------------------------------------------
// Submesh / mesh layout offsets used by ChangeTransparency (in BYTES, relative
// to the submesh pointer a2). Recovered from the decompile:
//   +12  polyCount (int)         a2[3]
//   +4   polyArray (int)         a2[1]  (poly stride 40, texture id @+20)
//   +16  meshObj   (int)         a2[4]  (meshObj+480 = clone-table capacity)
//   +376 currentColor (int)
//   +378 stateByte  (byte, bit0)
// ---------------------------------------------------------------------------

// ===========================================================================
// Public faithful entry points (Node5 view). See .cpp for 1:1 control flow.
// ===========================================================================

// 0x5b2cf8 — bounding sphere. Returns 1 if a radius was computed (writes
// *outRadius if non-null and *outCenter[3] if non-null). a2 = point in.
char ObjectComputeBoundingRadius(Node5* node, const float point[3],
                                 float* outRadius, float outCenter[3]);

// 0x5b2b70 — copy boneName into skeleton+180, validate against the bone table,
// rebuild bone matrices. Returns 1 on success, 0 on any guard failure.
char ObjectAttachToBone(Node5* node, const char* boneName);

// 0x5b7d14 — transform a point + euler triple from bone-local to parent space
// (or copy through when no parent bone). Writes outPoint[3] and outAngles[3].
void ObjectTransformPointToParent(Node5* node, const float point[3],
                                  float outPoint[3], float inAngles[3],
                                  float outAngles[3]);

// 0x5b7e24 — thunk: TransformPointToParent then SetPosition + SetWorldTranslation.
char ObjectApplyParentTransform(Node5* node, const float point[3],
                                float angles[3]);

// 0x5b7e54 — reparent under newParent preserving the world transform. Returns 1
// on success / no-op-cycle, 0 on bad args.
char ObjectReparentWithTransform(Node5* node, Node5* newParent, int extra);

// 0x5b2c70 — bind selected LOD mesh into +460, recompute, mark dirty.
int ObjectAssignMeshData(Node5* node, bool negativeScale);

// 0x5b4420 — dispose old mesh, load new by name, re-attach, rebuild light cache.
char ObjectRebindParentMesh(Node5* node, int freeArg, const char* meshName);

// 0x5b2710 — palette-swap one submesh to/from a transparency clone set.
//   color == 0xFF (and no high bit) => RESTORE; else APPLY color.
char ObjectChangeTransparency(Node5* node, void* submesh, int color);

// 0x5b2964 — drive ChangeTransparency over each submesh. colorPtr -> *colorPtr.
char ObjectChangeTransparencySubMeshes(Node5* node, int* colorPtr);

// 0x5b29b0 — walk subtree applying ChangeTransparencySubMeshes(color).
char ObjectApplyTransparencyTree(Node5* node, int color);

// 0x5b4274 — suspend toggle (named). enable!=0 + state1 -> restore via name copy.
char ObjectToggleSuspendStateNamed(Node5* node, char enable);

// 0x5b433c — suspend toggle (restore). Same shape, only state==5 refreshes light.
char ObjectRestoreSuspendState(Node5* node, char enable);

// 0x5b4258 — unlink + dispose. Returns 1, or 0 when node is null.
char ObjectDetachAndRelease(Node5* node);

// 0x5b0bf0 — set the active-camera global if node is a camera (byte533==3) and
// differs from the current one; invalidate the previous current. Returns the
// node pointer-as-int passed in (faithful eax passthrough), as bool-ish int.
int ObjectSetActiveCamera(Node5* node);

}  // namespace guild::sim
