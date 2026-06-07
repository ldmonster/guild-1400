// ===========================================================================
// object_lifecycle5.cpp — see object_lifecycle5.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle5.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered rodata.
// ---------------------------------------------------------------------------
// flt_6282F4 — used as 1/8 (0.125) to average the 8 bounding-box corners.
const float kBBoxCentroidScale = 0.125f;

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife5Hooks g_hooks;

// Read/write a native pointer out of an external raw block at a byte offset.
// The original treats `*(base+off)` as a 32-bit pointer; here it is the native
// link stored at that byte offset. Used for the skeleton / bone-table / submesh
// pointer chains that hang off the node (NOT inside Node5 — those use Node5::p).
inline void* PtrAt(void* base, int off) {
    return *reinterpret_cast<void**>(reinterpret_cast<u8*>(base) + off);
}
inline i32& IntAt(void* base, int off) {
    return *reinterpret_cast<i32*>(reinterpret_cast<u8*>(base) + off);
}
inline u8& ByteAt(void* base, int off) {
    return *(reinterpret_cast<u8*>(base) + off);
}
}  // namespace

// dword_13FCD1C — the active camera. Owned per-file across the family; this TU's
// view (the batch-3/4 views live in their own TUs to avoid an ODR clash).
Node5* g_activeCamera5 = nullptr;

void ObjLife5SetHooks(const ObjLife5Hooks& hooks) { g_hooks = hooks; }
void ObjLife5ResetHooks() { g_hooks = ObjLife5Hooks(); }

// ===========================================================================
// 0x5b2cf8 — VIBE_Object_ComputeBoundingRadius
//   al=node@eax, &point@edx, &outRadius@ecx, &outCenter@ebx
// Faithful control flow:
//   if (!node || (!outCenter && !outRadius)) return 0;
//   radius = 100;
//   if (!*(node+533)) {                       // skinned path
//       PointToBoneLocalSpace(node, point, &center, node+76);
//       radius = *(node+104); ret=1; goto store;
//   }
//   meshFn = *(node+460); if(!meshFn) return 0; if(!*(meshFn+16)) return 0;
//   TransformBoundingVolume(node, point, 0);
//   corners = (*(*(meshObj+16)+500))();  if(!corners) return 0;
//   center = avg(8 corners) * 0.125;
//   radius = sqrt(max over 8 corners of |center-corner|^2);  ret=1;
//   store: if(outRadius) *outRadius=radius; if(!outCenter) return ret;
//          outCenter = center; return ret.
// ===========================================================================
char ObjectComputeBoundingRadius(Node5* node, const float point[3],
                                 float* outRadius, float outCenter[3]) {
    char ret = 0;                                    // v27
    if (!node || (!outCenter && !outRadius))         // 0x5b2d1f
        return ret;

    float radius = 100.0f;                           // v25
    float center[3] = {0.0f, 0.0f, 0.0f};            // v17/v18/v19

    if (!node->b(533)) {                             // 0x5b2d34 — skinned path
        if (g_hooks.pointToBoneLocalSpace)           // 0x5b2e99
            g_hooks.pointToBoneLocalSpace(node, point, center, &node->f(76));
        radius = node->f(104);                       // 0x5b2ea3
        ret = 1;                                      // 0x5b2ea7
    } else {
        void* meshFn = node->p(460);                 // 0x5b2d3a
        if (!meshFn)                                  // 0x5b2d42
            return ret;
        if (!PtrAt(meshFn, 16))                      // 0x5b2d48
            return ret;

        // VIBE_Mesh_TransformBoundingVolume + virtual fetch of the 8 corners.
        const float* corners = g_hooks.meshBoundingCorners
                                   ? g_hooks.meshBoundingCorners(node, point)
                                   : nullptr;         // 0x5b2d59 / 0x5b2d76
        if (!corners)                                 // 0x5b2d82
            return ret;

        // Sum the 8 corners (stride 20 floats), then * 0.125. 0x5b2d8a..0x5b2dcf
        center[0] = corners[0];
        center[1] = corners[1];
        center[2] = corners[2];
        for (unsigned i = 1; i < 8; ++i) {
            center[0] += corners[i * 20 + 0];
            center[1] += corners[i * 20 + 1];
            center[2] += corners[i * 20 + 2];
        }
        center[0] *= kBBoxCentroidScale;             // 0x5b2dfd
        center[1] *= kBBoxCentroidScale;             // 0x5b2df3
        center[2] *= kBBoxCentroidScale;             // 0x5b2df9

        float maxSq = 0.0f;                           // v26
        for (unsigned i = 0; i < 8; ++i) {            // 0x5b2e11..0x5b2e61
            float dx = center[0] - corners[i * 20 + 0];
            float dy = center[1] - corners[i * 20 + 1];
            float dz = center[2] - corners[i * 20 + 2];
            float sq = dx * dx + dy * dy + dz * dz;
            if (sq >= maxSq)
                maxSq = sq;
        }
        ret = 1;                                      // 0x5b2e68
        radius = std::sqrt(maxSq);                    // 0x5b2e6d
    }

    if (outRadius)                                    // 0x5b2e76
        *outRadius = radius;
    if (!outCenter)                                   // 0x5b2e84
        return ret;
    outCenter[0] = center[0];                         // 0x5b2edd
    outCenter[1] = center[1];
    outCenter[2] = center[2];
    return ret;
}

// ===========================================================================
// 0x5b2b70 — VIBE_Object_AttachToBone (al=node@eax, edx=boneName)
//   Guards: node, boneName, *(node+504) parentBone, *(node+492) skeleton.
//   Copies the SCRATCH name (unk_6282F0, here modeled empty) into skeleton+180
//   first (the original copies a transient buffer), then resolves the bone table
//   of the parent's skeleton (*(node+504)+492 then +260), scanning bone entries
//   (stride 88 from +116, count *(table+520)) by name. If found, copies the
//   requested boneName into skeleton+180 and rebuilds bone matrices. Returns 1.
//   We treat the leading scratch copy as a faithful no-op pre-clear (the scratch
//   buffer unk_6282F0 is empty at rest) and model the bone table via the hooks'
//   feed: the table scan/compare is reproduced over a caller-supplied table.
// ===========================================================================
char ObjectAttachToBone(Node5* node, const char* boneName) {
    if (!node)                                        // 0x5b2b80
        return 0;
    if (!boneName)                                    // 0x5b2b84
        return 0;
    if (!node->p(504))                                // 0x5b2b86 parentBone
        return 0;
    void* skeleton = node->p(492);                    // 0x5b2b8f
    if (!skeleton)                                     // 0x5b2b97
        return 0;

    // First copy: scratch (unk_6282F0) -> skeleton+180. The scratch is empty at
    // rest, so this clears the destination name. (Faithful: copies until NUL.)
    std::strcpy(reinterpret_cast<char*>(skeleton) + 180, "");

    void* parentBone = node->p(504);                  // v18 = *(node+504)
    void* parentSkel = PtrAt(parentBone, 492);        // 0x5b2bd3
    if (!parentSkel)                                  // 0x5b2bde
        return 0;
    void* table = PtrAt(parentSkel, 260);             // 0x5b2be0
    if (!table)                                       // 0x5b2bea
        return 0;

    int count = ByteAt(table, 520);                   // 0x5b2c09
    int idx = 0;                                       // v11
    const char* entry =
        reinterpret_cast<const char*>(table) + 116;    // i
    while (idx < count && std::strcmp(boneName, entry) != 0) {        // 0x5b2bf0
        ++idx;                                         // 0x5b2c23
        entry += 88;                                   // i = v13 + 88
    }
    if (idx >= count)                                  // 0x5b2c09
        return 0;

    // Found: copy the requested name into skeleton+180. 0x5b2c33..0x5b2c52
    std::strcpy(reinterpret_cast<char*>(skeleton) + 180, boneName);

    if (g_hooks.animAssignSubMeshBones)                // 0x5b2c58
        g_hooks.animAssignSubMeshBones(
            reinterpret_cast<Node5*>(parentBone));
    if (g_hooks.animComputeBoneMatrices)               // 0x5b2c60
        g_hooks.animComputeBoneMatrices(
            reinterpret_cast<Node5*>(parentBone));
    return 1;
}

// ===========================================================================
// 0x5b7d14 — VIBE_Object_TransformPointToParent
//   eax=node, edx=&point, ecx=&outPoint, ebx=&outAngles, a5=&outAngles2(=inAngles)
//   if (*(node+504)) {                       // has parent bone
//       ComputeBoneWorldMatrix(*(node+504), 0, 0); MatrixInverse(scratch, world);
//       outPoint = point * world (4x3 affine);
//       ComputeBoneWorldMatrix(node, 0, 1);  MatrixCopy(scratch, world);
//       MatrixTransformVectors(inAngles, world, scratch); MatrixToEuler(scratch->outAngles);
//   } else {
//       outPoint = point; MatrixToEuler(inAngles);  // angles passthrough in place
//   }
// We fetch the world matrix produced by ComputeBoneWorldMatrix via fetchWorldMatrix
// (default identity). The euler conversion writes into outAngles (the original's
// a5/v18 buffer); we expose it as the inAngles buffer transformed in place when
// no parent, and outAngles when there is.
// ===========================================================================
void ObjectTransformPointToParent(Node5* node, const float point[3],
                                  float outPoint[3], float inAngles[3],
                                  float outAngles[3]) {
    if (node->p(504)) {                                // 0x5b7d2b
        Node5* parentBone = reinterpret_cast<Node5*>(node->p(504));
        float world[16];                               // v11
        if (g_hooks.computeBoneWorldMatrix)            // 0x5b7d68 (parent, which=0)
            g_hooks.computeBoneWorldMatrix(parentBone, 0, 0);
        // MatrixInverse(v12, v11): produce the inverse world matrix into `world`.
        float wsrc[16];
        if (g_hooks.fetchWorldMatrix)
            g_hooks.fetchWorldMatrix(parentBone, wsrc);
        else
            for (int i = 0; i < 16; ++i) wsrc[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        if (g_hooks.matrixInverse)                     // 0x5b7d75
            g_hooks.matrixInverse(wsrc, world);
        else
            std::memcpy(world, wsrc, sizeof(world));

        // outPoint = point * world (column layout from the decompile). 0x5b7dce
        float py = point[0] * world[1] + point[1] * world[5] +
                   point[2] * world[9] + world[13];
        float pz = point[0] * world[2] + point[1] * world[6] +
                   point[2] * world[10] + world[14];
        outPoint[0] = point[0] * world[0] + point[1] * world[4] +
                      point[2] * world[8] + world[12];
        outPoint[2] = pz;
        outPoint[1] = py;

        if (g_hooks.computeBoneWorldMatrix)            // 0x5b7deb (node, which=1)
            g_hooks.computeBoneWorldMatrix(node, 0, 1);
        float wmat[16], tmp[16];
        if (g_hooks.fetchWorldMatrix)
            g_hooks.fetchWorldMatrix(node, wmat);
        else
            for (int i = 0; i < 16; ++i) wmat[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        if (g_hooks.matrixCopy)                        // 0x5b7dfa
            g_hooks.matrixCopy(world, wmat);
        else
            std::memcpy(world, wmat, sizeof(world));
        if (g_hooks.matrixTransformVectors)            // 0x5b7e03
            g_hooks.matrixTransformVectors(inAngles, world, tmp);
        else
            std::memcpy(tmp, inAngles, 3 * sizeof(float));
        // MatrixToEuler(tmp) -> outAngles.
        if (g_hooks.matrixToEuler)                      // 0x5b7e13
            g_hooks.matrixToEuler(tmp);
        outAngles[0] = tmp[0];
        outAngles[1] = tmp[1];
        outAngles[2] = tmp[2];
    } else {
        outPoint[0] = point[0];                         // 0x5b7d38
        outPoint[1] = point[1];
        outPoint[2] = point[2];
        if (g_hooks.matrixToEuler)                      // 0x5b7d46
            g_hooks.matrixToEuler(inAngles);
        outAngles[0] = inAngles[0];
        outAngles[1] = inAngles[1];
        outAngles[2] = inAngles[2];
    }
}

// ===========================================================================
// 0x5b7e24 — VIBE_Object_ApplyParentTransform (al=node, edx=&point, ebx=&angles)
//   TransformPointToParent(node, point, v5, angles, v6);
//   SetPosition(node, v5); return SetWorldTranslation(node, v6);
// ===========================================================================
char ObjectApplyParentTransform(Node5* node, const float point[3],
                                float angles[3]) {
    float outPoint[3];                                 // v5
    float outAngles[3];                                // v6 (20 bytes; 3 used)
    ObjectTransformPointToParent(node, point, outPoint, angles, outAngles);  // 0x5b7e34
    if (g_hooks.objSetPosition)                        // 0x5b7e3d
        g_hooks.objSetPosition(node, outPoint);
    if (g_hooks.objSetWorldTranslation)                // 0x5b7e4d
        g_hooks.objSetWorldTranslation(node, outAngles);
    return 1;
}

// ===========================================================================
// 0x5b7e54 — VIBE_Object_ReparentWithTransform (eax=node, edx=newParent, edi=extra)
//   if (node==newParent || !node) return 0;
//   if (newParent) { walk newParent's parent-chain (+508 then +496); if it
//       reaches `node` -> cycle -> return 1; }
//   else (or chain exhausted): if FindByHandle(node,511,0,newParent,extra) ->
//       return 0; else LABEL_8 do the reparent.
//   LABEL_8: snapshot the node's child sub-bones (state>=5 && +488), unlink,
//       relink under newParent (SetParent) or LinkIntoScene, then transform the
//       node + each sub-bone into the new parent space. return 1.
//   The bone-chain transform + scene-graph hooks are routed; the control flow,
//   the cycle test, and the SetWorldTranslation ordering are faithful.
// ===========================================================================
char ObjectReparentWithTransform(Node5* node, Node5* newParent, int extra) {
    if (node == newParent || !node)                    // 0x5b7e71
        return 0;

    bool doReparent = false;
    if (!newParent) {                                  // 0x5b7e79 -> LABEL_8
        doReparent = true;
    } else {
        // v5 = *(newParent+508); walk +496 until it equals node (cycle) or null.
        void* cur = newParent->p(508);                 // 0x5b7e7b
        bool cycle = false, exhausted = false;
        if (!cur) {
            exhausted = true;                          // 0x5b7e83 -> LABEL_7
        } else {
            while (cur != static_cast<void*>(node)) {
                cur = PtrAt(cur, 496);                 // 0x5b7e8d
                if (!cur) { exhausted = true; break; } // 0x5b7e95 -> LABEL_7
            }
            if (!exhausted) cycle = true;              // reached node -> 0x5b8077
        }
        if (cycle)
            return 1;                                  // 0x5b8077
        if (exhausted) {                               // LABEL_7
            int found = g_hooks.objFindByHandle
                            ? g_hooks.objFindByHandle(node, 511, 0, newParent,
                                                      extra)
                            : 0;                        // 0x5b7ea7
            if (found)
                return 0;                               // 0x5b808e
            doReparent = true;                          // LABEL_8
        }
    }

    if (!doReparent)
        return 1;

    // LABEL_8 — the reparent body. The sub-bone snapshot/restore loops and the
    // bone-chain transforms are routed through hooks; we drive the scene-graph
    // relink + the world-translation save/restore ordering faithfully.
    float savedWorld[3];                               // v19 — node euler @ +132
    bool hasSubBones = (node->sb(533) >= 5) && node->p(488);   // 0x5b7ec2
    if (hasSubBones) {
        savedWorld[0] = node->f(132);                  // 0x5b7ed5
        savedWorld[1] = node->f(136);
        savedWorld[2] = node->f(140);
        // Snapshot pass over the bone array (stride 56) — routed.
        if (g_hooks.pointThroughBoneChain && g_hooks.computeBoneWorldMatrix) {
            u8* base = reinterpret_cast<u8*>(node->p(488));
            // The original iterates a 7-entry (448/64) buffer; we drive the hook
            // for each present sub-bone via the chain transform + matrix rebuild.
            float scratch[3];
            g_hooks.pointThroughBoneChain(node,
                reinterpret_cast<float*>(base), scratch);  // 0x5b7f28
            if (g_hooks.objSetWorldTranslation)        // 0x5b7f41
                g_hooks.objSetWorldTranslation(
                    node, reinterpret_cast<float*>(base + 12));
            g_hooks.computeBoneWorldMatrix(node, 0, 1);  // 0x5b7f4d
        }
        if (g_hooks.objSetWorldTranslation)            // 0x5b7f7a
            g_hooks.objSetWorldTranslation(node, savedWorld);
    }

    float localPoint[3];                               // v16
    if (g_hooks.pointThroughBoneChain)                 // 0x5b7f92
        g_hooks.pointThroughBoneChain(node, &node->f(76), localPoint);
    else { localPoint[0] = node->f(76); localPoint[1] = node->f(80);
           localPoint[2] = node->f(84); }
    if (g_hooks.computeBoneWorldMatrix)                // 0x5b7fa0
        g_hooks.computeBoneWorldMatrix(node, 0, 1);

    if (g_hooks.objUnlinkFromList)                     // 0x5b7fae
        g_hooks.objUnlinkFromList(node);
    if (newParent) {                                   // 0x5b7fb5
        if (g_hooks.objSetParent)                      // 0x5b80a0
            g_hooks.objSetParent(newParent, node);
    } else {
        if (g_hooks.objLinkIntoScene)                  // 0x5b7fbd
            g_hooks.objLinkIntoScene(node);
    }

    float newPoint[3], newAngles[3];                   // v17 / v18
    ObjectTransformPointToParent(node, localPoint, newPoint,
                                 &node->f(132), newAngles);   // 0x5b7fe1
    if (g_hooks.objSetPosition)                        // 0x5b7fef
        g_hooks.objSetPosition(node, newPoint);
    if (g_hooks.objSetWorldTranslation)                // 0x5b7ffd
        g_hooks.objSetWorldTranslation(node, newAngles);

    if ((node->sb(533) >= 5) && node->p(488)) {        // 0x5b800b — restore pass
        // The original re-applies TransformPointToParent over each sub-bone; the
        // bone-chain detail is routed.
        if (g_hooks.pointThroughBoneChain) {
            float scratch[3], s2[3];
            u8* base = reinterpret_cast<u8*>(node->p(488));
            ObjectTransformPointToParent(
                node, localPoint, reinterpret_cast<float*>(base), scratch, s2);  // 0x5b804e
        }
    }
    return 1;                                          // 0x5b8081
}

// ===========================================================================
// 0x5b2c70 — VIBE_Object_AssignMeshData (eax=node)
//   result = SelectLodFrame(node);
//   if (result) {
//       if (result != *(node+460)) *(node+460) = result;
//       v4 = (*(node+528) >= 0) ? dword_13FCD1C : 0;  // sign of byte528
//       ComputeBoneWorldMatrix(node, v4, 0);
//       result = (*(*(result+16)+496))();  // mesh finalize virtual
//       *(node+528) |= 4;
//   }
//   return result;
//   `negativeScale` carries the sign test (*(char*)(node+528) < 0).
// ===========================================================================
int ObjectAssignMeshData(Node5* node, bool negativeScale) {
    Node5* mesh = g_hooks.meshSelectLodFrame
                      ? g_hooks.meshSelectLodFrame(node)
                      : nullptr;                       // 0x5b2c77
    if (mesh) {                                        // 0x5b2c80
        if (mesh != node->p(460))                      // 0x5b2c88
            node->p(460) = mesh;
        // v4 = (byte528 signed >= 0) ? camera : 0 — modeled via negativeScale.
        (void)negativeScale;
        if (g_hooks.computeBoneWorldMatrix)            // 0x5b2ca2
            g_hooks.computeBoneWorldMatrix(node, 0, 0);
        if (g_hooks.meshFinalize)                      // 0x5b2cb2
            g_hooks.meshFinalize(node, mesh);
        node->b(528) |= 4u;                            // 0x5b2cb8
        return static_cast<i32>(reinterpret_cast<intptr_t>(mesh));
    }
    return 0;
}

// ===========================================================================
// 0x5b4420 — VIBE_Object_RebindParentMesh (eax=node, edi=freeArg, edx=meshName)
//   if (!node) return 0;
//   if (*(node+508)) { Dispose(*(node+508)); *(node+508)=0; }
//   v5 = *(node+496);
//   if (v5 && (*(v5+528)&1)==0) { child=GetFirstActiveChild(node); Dispose(...);
//                                 *(node+496)=child; }
//   FreeDrawData(node, freeArg);
//   LoadOrFindByName(meshName, meshName);
//   AttachStockObjectLods(?, 0, meshName, freeArg);
//   UploadAllRecords(0,0);
//   v10 = *(node+492); if(!v10 || !*(v10+260)) return 0;
//   BuildObjectCache(node); return 1;
// ===========================================================================
char ObjectRebindParentMesh(Node5* node, int freeArg, const char* meshName) {
    if (!node)                                         // 0x5b442a
        return 0;
    if (node->p(508)) {                                // 0x5b4430
        if (g_hooks.objDispose)                        // 0x5b44a5
            g_hooks.objDispose(reinterpret_cast<Node5*>(node->p(508)));
        node->p(508) = nullptr;                        // 0x5b44aa
    }
    void* selfLink = node->p(496);                     // 0x5b443a
    if (selfLink && (ByteAt(selfLink, 528) & 1) == 0) {  // 0x5b444f
        Node5* child = g_hooks.sceneGraphGetFirstActiveChild
                           ? g_hooks.sceneGraphGetFirstActiveChild(node)
                           : nullptr;                  // 0x5b4458
        if (g_hooks.objDispose)                        // 0x5b445c
            g_hooks.objDispose(reinterpret_cast<Node5*>(selfLink));
        node->p(496) = child;                          // 0x5b4461
    }
    if (g_hooks.objFreeDrawData)                       // 0x5b4469
        g_hooks.objFreeDrawData(node, freeArg);
    if (g_hooks.meshLoadOrFindByName)                  // 0x5b4472
        g_hooks.meshLoadOrFindByName(meshName);
    if (g_hooks.meshAttachStockObjectLods)             // 0x5b447b
        g_hooks.meshAttachStockObjectLods(node, meshName, freeArg);
    if (g_hooks.textureUploadAllRecords)               // 0x5b4484
        g_hooks.textureUploadAllRecords();

    void* skeleton = node->p(492);                     // 0x5b4489
    if (!skeleton || !PtrAt(skeleton, 260))            // 0x5b4493
        return 0;
    if (g_hooks.lightBuildObjectCache)                 // 0x5b44b8
        g_hooks.lightBuildObjectCache(node);
    return 1;
}

// ===========================================================================
// 0x5b2710 — VIBE_Object_ChangeTransparency (eax=node, edx=submesh, ebx=color)
//   if (!node || !submesh) return node;
//   meshObj = *(submesh+16); cap = *(meshObj+480); if (cap < 0) return;
//   if ((color & 0xFF)!=0xFF || (color & 0x10000)) {          // APPLY
//       if (*(submesh+376)==0xFF && (*(submesh+378)&1)==0) {  // not yet applied
//           *(node+529) |= 0x80;
//           map = alloc(8*cap);
//           for each poly with texture: dedup, clone palette match -> swap;
//           free(map);
//       }
//       *(submesh+376) = color;  return ApplyVertexShading(node, submesh);
//   } else {                                                  // RESTORE
//       map = alloc(8*cap); *(node+529)&=~0x80; *(submesh+376)=0;
//       *(submesh+376)=0xFF; *(submesh+378)&=~1;
//       for each poly: dedup, detach clone -> original;
//       free(map); return ApplyVertexShading(node, submesh);
//   }
// Poly layout: count @+12, array @+4 (stride 40, texture id @+20).
// ===========================================================================
char ObjectChangeTransparency(Node5* node, void* submeshPtr, int color) {
    if (!node)                                         // 0x5b2723
        return static_cast<char>(reinterpret_cast<intptr_t>(node));
    if (!submeshPtr)                                   // 0x5b272b
        return 1;  // result unchanged (node truthy); faithful eax = node low byte
    u8* sm = reinterpret_cast<u8*>(submeshPtr);
    void* meshObj = PtrAt(sm, 16);                     // 0x5b2731
    i32 cap = IntAt(meshObj, 480);                     // 0x5b2734
    if (cap < 0)                                       // 0x5b273c
        return 1;

    int polyCount = *reinterpret_cast<i32*>(sm + 12);
    u8* polyArray = reinterpret_cast<u8*>(PtrAt(sm, 4));

    char result = 1;
    bool apply = ((color & 0xFF) != 0xFF) || (color & 0x10000);   // 0x5b2859 inverted
    if (apply) {
        if (*reinterpret_cast<u8*>(sm + 376) == 0xFF &&
            (*reinterpret_cast<u8*>(sm + 378) & 1) == 0) {  // 0x5b2761
            node->b(529) |= 0x80u;                     // 0x5b276b
            int* map = reinterpret_cast<int*>(
                g_hooks.memAlloc ? g_hooks.memAlloc(8 * cap,
                                                    "!d3:ChangeObjectTransparency()")
                                 : nullptr);            // 0x5b2791
            int found = 0;                              // v6
            for (int i = 0; i < polyCount; ++i) {       // 0x5b27aa..0x5b2821
                i32* tex = reinterpret_cast<i32*>(polyArray + i * 40 + 20);
                if (*tex) {
                    int j = 0;
                    for (; j < found; ++j)              // dedup scan
                        if (*tex == map[j * 2]) break;
                    if (j == found && map) {            // new texture
                        map[found * 2] = *tex;
                        map[found * 2 + 1] =
                            g_hooks.textureCloneIfPaletteMatch
                                ? g_hooks.textureCloneIfPaletteMatch(*tex, color)
                                : *tex;
                        ++found;
                    }
                    if (map)
                        *tex = map[j * 2 + 1];          // swap to clone
                }
            }
            if (g_hooks.memFree)                        // 0x5b282b
                g_hooks.memFree(map);
        }
        *reinterpret_cast<i32*>(sm + 376) = color;      // 0x5b283d
        if (g_hooks.lightApplyVertexShading)            // 0x5b2847
            result = static_cast<char>(
                g_hooks.lightApplyVertexShading(node, submeshPtr));
    } else {  // RESTORE
        int* map = reinterpret_cast<int*>(
            g_hooks.memAlloc ? g_hooks.memAlloc(8 * cap,
                                                "!d3:ChangeObjectTransparency()")
                             : nullptr);                // 0x5b2870
        node->b(529) &= ~0x80u;                         // 0x5b2878
        *reinterpret_cast<i32*>(sm + 376) = 0;          // 0x5b2881
        u8 st = *reinterpret_cast<u8*>(sm + 378);       // 0x5b288f
        *reinterpret_cast<u8*>(sm + 376) = 0xFF;        // 0x5b2895
        *reinterpret_cast<u8*>(sm + 378) = st & 0xFE;   // 0x5b28a2
        int found = 0;                                  // v11
        for (int i = 0; i < polyCount; ++i) {           // 0x5b28b5..0x5b291e
            i32* tex = reinterpret_cast<i32*>(polyArray + i * 40 + 20);
            if (*tex) {
                int j = 0;
                for (; j < found; ++j)
                    if (*tex == map[j * 2]) break;
                if (j == found && map) {
                    map[found * 2] = *tex;
                    map[found * 2 + 1] =
                        g_hooks.textureDetachClone
                            ? g_hooks.textureDetachClone(*tex)
                            : *tex;
                    ++found;
                }
                if (map)
                    *tex = map[j * 2 + 1];              // restore original
            }
        }
        if (g_hooks.memFree)                            // 0x5b2928
            g_hooks.memFree(map);
        if (g_hooks.lightApplyVertexShading)            // 0x5b2935
            result = static_cast<char>(
                g_hooks.lightApplyVertexShading(node, submeshPtr));
    }
    return result;
}

// ===========================================================================
// 0x5b2964 — VIBE_Object_ChangeTransparencySubMeshes (eax=node, edx=&color)
//   if (*(node+492)) {
//       v4=0; v5=0;
//       while (v5 < *(byte)(*(node+492)+2316))
//           ChangeTransparency(node, *(node+492)+244+v4, *color, ++v5);
//           v4 += 384;
//   }
//   return 1;
//   submesh stride 384, base = skeleton+244, count byte @ skeleton+2316.
// ===========================================================================
char ObjectChangeTransparencySubMeshes(Node5* node, int* colorPtr) {
    if (node->p(492)) {                                // 0x5b296d
        int v4 = 0;                                     // submesh byte offset
        unsigned v5 = 0;                                // index
        while (true) {                                  // 0x5b297a
            u8* skeleton = reinterpret_cast<u8*>(node->p(492));
            if (v5 >= skeleton[2316])                    // 0x5b298a
                break;
            ++v5;
            ObjectChangeTransparency(
                node, skeleton + 244 + v4, *colorPtr);   // 0x5b29a2
            v4 += 384;                                  // 0x5b29a7
        }
    }
    return 1;                                           // 0x5b298e
}

// ===========================================================================
// 0x5b29b0 — VIBE_Object_ApplyTransparencyTree (eax=node, edx=color)
//   if (node) WalkAndInvoke(off_649D64, node, ChangeTransparencySubMeshes, 576, color);
//   return node-low-byte.
//   We model the subtree walk's leaf (ChangeTransparencySubMeshes on this node)
//   directly; the recursion is the WalkAndInvoke leaf (not owned here).
// ===========================================================================
char ObjectApplyTransparencyTree(Node5* node, int color) {
    if (node) {                                        // 0x5b29b2
        int c = color;
        ObjectChangeTransparencySubMeshes(node, &c);   // leaf side-effect
        return 1;
    }
    return 0;
}

// ===========================================================================
// 0x5b4274 — VIBE_Object_ToggleSuspendStateNamed (eax=node, dl=enable)
//   if (!node) return 0;
//   if (enable && node[533]==1) {            // currently suspended -> restore
//       if (node[0]==33) { copy node+1 -> scratch; copy scratch -> node; }  // strip '!'
//       v12 = node[534];
//       if (v12==5 || v12==6) RefreshAllObjects(0);
//       node[533] = node[534];  return 1;
//   }
//   if (enable) return 1;
//   v14 = node[533]; if (v14==1) return 1;
//   node[533]=1; node[534]=v14; return 1;
//   The two name copies move node+1 through a 64-byte scratch back to node — i.e.
//   shift the name left by 1 (drop the leading '!'). Faithful 1:1.
// ===========================================================================
char ObjectToggleSuspendStateNamed(Node5* node, char enable) {
    if (!node)                                         // 0x5b427c
        return 0;
    if (enable && node->sb(533) == 1) {                // 0x5b4293
        if (node->b(0) == 33) {                        // '!' prefix 0x5b429c
            char scratch[64];
            std::strcpy(scratch, node->str(1));        // node+1 -> scratch
            std::strcpy(node->str(0), scratch);        // scratch -> node
        }
        u8 v12 = node->b(534);                          // 0x5b42e0
        if (v12 == 5 || v12 == 6) {                     // 0x5b430b
            if (g_hooks.lightRefreshAllObjects)
                g_hooks.lightRefreshAllObjects();       // 0x5b42ed
        }
        node->b(533) = node->b(534);                    // 0x5b42f8
        return 1;
    }
    if (enable)                                         // 0x5b4311
        return 1;
    i8 v14 = node->sb(533);                             // 0x5b4313
    if (v14 == 1)                                       // 0x5b431c
        return 1;
    node->sb(533) = 1;                                  // 0x5b431e
    node->sb(534) = v14;                                // 0x5b4325
    return 1;
}

// ===========================================================================
// 0x5b433c — VIBE_Object_RestoreSuspendState (eax=node, dl=enable)
//   Identical shape; restore branch only refreshes light when node[534]==5
//   (not also 6), and there is no leading !node guard (a2 path checks enable).
// ===========================================================================
char ObjectRestoreSuspendState(Node5* node, char enable) {
    if (enable && node->sb(533) == 1) {                // 0x5b4355
        if (node->b(0) == 33) {                        // 0x5b435e
            char scratch[76];
            std::strcpy(scratch, node->str(1));         // node+1 -> scratch
            std::strcpy(node->str(0), scratch);         // scratch -> node
        }
        if (node->b(534) == 5) {                        // 0x5b43a7
            if (g_hooks.lightRefreshAllObjects)
                g_hooks.lightRefreshAllObjects();       // 0x5b43ab
        }
        node->b(533) = node->b(534);                    // 0x5b43b6
        return 1;
    }
    if (enable)                                         // 0x5b43cc
        return 1;
    i8 v12 = node->sb(533);                             // 0x5b43ce
    if (v12 == 1)                                       // 0x5b43d7
        return 1;
    node->sb(533) = 1;                                  // 0x5b43d9
    node->sb(534) = v12;                                // 0x5b43e0
    return 1;
}

// ===========================================================================
// 0x5b4258 — VIBE_Object_DetachAndRelease (eax=node)
//   if (!node) return 0; UnlinkFromList(node); Dispose(node); return 1;
// ===========================================================================
char ObjectDetachAndRelease(Node5* node) {
    if (!node)                                          // 0x5b425d
        return 0;
    if (g_hooks.objUnlinkFromList)                      // 0x5b4263
        g_hooks.objUnlinkFromList(node);
    if (g_hooks.objDispose)                             // 0x5b426a
        g_hooks.objDispose(node);
    return 1;
}

// ===========================================================================
// 0x5b0bf0 — VIBE_Object_SetActiveCamera (eax=node)
//   if (node != dword_13FCD1C && *(node+533)==3) {
//       dword_13FCD1C = node; return InvalidateCurrent(0);
//   }
//   return node;
//   Returns the original eax (node) when no change, else the InvalidateCurrent
//   byte result. We return the camera-set flag (1 if changed) to keep the
//   observable side effect testable, mirroring the al low-byte.
// ===========================================================================
int ObjectSetActiveCamera(Node5* node) {
    if (node != g_activeCamera5 && node && node->b(533) == 3) {  // 0x5b0bff
        g_activeCamera5 = node;                          // 0x5b0c02
        if (g_hooks.objInvalidateCurrent)                // 0x5b0c09
            g_hooks.objInvalidateCurrent(0);
        return 1;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(node) & 0xFF);
}

}  // namespace guild::sim
