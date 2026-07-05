#pragma once
// =============================================================================
// guild::render — OBJECT / SKELETAL animation cluster (wave-19, W19-OBJANIM).
//
// 1:1 reconstruction of the object/skeletal-anim attach/stock core reached from
// the per-frame loop (0x4c09a0) and engine init:
//
//   gilde.exe 0x5cef14  VIBE_Anim_CreateObjectAnim        -> CreateObjectAnim
//   gilde.exe 0x5d0b64  VIBE_Anim_AttachToBone            -> AttachToBone
//   gilde.exe 0x5d0d38  VIBE_Anim_PruneExpiredAttachments -> PruneExpiredAttachments
//   gilde.exe 0x5cec00  VIBE_Anim_FreeObjAnimData         -> FreeObjAnimData
//   gilde.exe 0x5cf114  VIBE_Anim_FindFreeMeshSlot        -> FindFreeMeshSlot
//   gilde.exe 0x5d3858  VIBE_Anim_LoadStreamToStock       -> LoadStreamToStock
//
// Genuine leaves reconstructed alongside (so the tree reaches them):
//   gilde.exe 0x5cb8f0  VIBE_Util_StrCmpNoCase   -> StrCmpNoCase
//   gilde.exe 0x5d3f10  VIBE_Util_StrCmp         -> reused (anim_recon4_mesh_lru UtilStrCmp)
//   gilde.exe 0x5ccf18  VIBE_Anim_AdvanceFrameIndex -> AdvanceFrameIndex
//
// The object-anim KEYFRAME math (Catmull-Rom tangents + Hermite sample) is in
// render/object_anim.h; this module is the ATTACH / STOCK-LIST / FRAME-INDEX
// state machine the engine wraps around it. The keyframe array build inside
// CreateObjectAnim is the qmemcpy of the source descriptor + the per-frame "*3"
// frame-count fixup; the final tangent build is delegated to BuildFrameTangents.
//
// Engine edges that touch live scene/mesh state (mesh-data alloc/free, submesh
// bone assignment, world-matrix compute, .baf binary loader) are injected via a
// hooks struct with inert defaults, so the PURE list/slot/frame-index/descriptor
// logic is reconstructed 1:1 and headless-testable (the established pattern used
// by object_throwbomb / anim_recon4_mesh_lru).
// =============================================================================
#include "guild/common/types.h"
#include "render/skeleton.h"    // VIBE_Anim_AdvanceFrameIndex @0x5ccf18 (reused, no ODR)
#include "util/string_ops.h"    // VIBE_Util_StrCmpNoCase @0x5cb8f0 (reused, no ODR)

#include <string>
#include <vector>

namespace guild::render {

using namespace guild;  // u8/u16/u32/i32 ...

// gilde.exe 0x5cb8f0 VIBE_Util_StrCmpNoCase lives in guild::util (string_ops.cpp);
// gilde.exe 0x5ccf18 VIBE_Anim_AdvanceFrameIndex lives in guild::render (skeleton.cpp).
// Both are REUSED here (AnimObject golden tests pin them via these decls) — never
// redefined (one-definition rule). Local aliases for readability:
using guild::util::StrCmpNoCase;
// AdvanceFrameIndex(flags, cur, last/hiStop, first/loStop, count/total) is already
// declared in render/skeleton.h within this namespace.

// ===========================================================================
// Object-anim record (the 0x3C = 60-byte block CreateObjectAnim allocates and
// stores at node+464). Field offsets are the original's; we keep them explicit
// so a test can pin byte-for-byte. `frames` is the 88-byte keyframe array the
// original allocates at +56 (frameCount entries); modeled by value here.
// ===========================================================================
struct ObjAnimRecord {
    i32  frameCount = 0;   // +0    *(u32*)rec   (the descriptor's a4)
    i32  curFrame   = 0;   // +4    current keyframe index
    i32  nextFrame  = 1;   // +8    next keyframe index (AdvanceFrameIndex result)
    i32  subFrame   = 0;   // +12   intra-segment sub-frame
    // +16..19 unused here
    i32  src        = 0;   // +44 (a2 source descriptor handle, stored at *(rec+11))
    u8   ctrl45     = 0;   // +45   control byte: bit1 (0x02) = looped/play-to-end
    u8   ctrl46     = 0;   // +46   flag byte: 0x10 set, 0x20 cleared (twice)
    i32  scale      = 100; // +16   *(rec+4) = 100 (percent)
    u32  weight     = 0x3f800000; // +48 *(rec+12) = 1065353216 (=1.0f bits)
    // pose snapshot copied from the parent node (node+76/80/84 pos, +132/136/140 rot):
    i32  pos[3]     = {0, 0, 0};   // +20/+24/+28
    i32  rot[3]     = {0, 0, 0};   // +32/+36/+40
    std::vector<i32> frames;       // the +56 keyframe array, frame[i] = duration*3 fixup
};

// ===========================================================================
// Bone-attachment channel block. AttachToBone writes a 116-byte channel record
// at (model + 28 + 116*slot). We model the fields it actually sets.
// ===========================================================================
struct BoneAttachChannel {
    bool  used      = false;
    i32   curFrame  = 0;    // +0
    i32   nextFrame = 1;    // +4   AdvanceFrameIndex result (loop) / 1 (non-loop)
    i32   hiStop    = 0;    // +8   *(stream+4*last - ...) - 1 (loop) / 0
    i32   weightCur = 0;    // +12  stream+336 / 0 (if stream+361)
    i32   blendA    = 0;    // +16  = 0
    i32   frameA    = -1;   // +20  = -1 (mirror of +28)
    i32   frameB    = -1;   // +24  = -1
    i32   markFrame = -1;   // +28  = -1
    i32   accumA    = 0;    // +48  = 0
    i32   accumB    = 0;    // +52  mirror of +56
    i32   speed     = 0;    // +56  = 0
    i32   blendCur  = 0x3f800000; // +64 mirror of +68
    i32   blendDst  = 0x3f800000; // +68 mirror of +72
    i32   blendInit = 0x3f800000; // +72 = 1065353216
    i32   weight    = 0;    // +60  = 100
    struct AnimStream* stream = nullptr;  // +104 the stock-stream record played (32-bit ptr in orig)
    i32   nameId    = 0;    // +108 a2 (the requested anim name/id)
    u8    flags109  = 0;    // +109
    u8    flags110  = 0;    // +110
};

// ===========================================================================
// A node that can carry object-anim + bone attachments. We expose only the
// fields the six functions touch; offsets are documented from the original.
// ===========================================================================
struct AnimNode {
    // +464: object-anim record pointer (0 = none). Modeled by value/owned ptr.
    ObjAnimRecord* objAnim = nullptr;
    u8   classByte = 0;          // +533 (object class: 3 or 4 => createable)
    i32  pos[3]    = {0, 0, 0};  // +76/+80/+84
    i32  rot[3]    = {0, 0, 0};  // +132/+136/+140
    i32  drawData  = 0;          // +24  (drawable handle; passed to submesh/matrix)
    i32  meshType  = 0;          // +8   compared against stream+320
    i32  meshNormalsSrc = 0;     // +16  (normals source; CalculateAnimNormals input)
    bool boneActive = false;     // +380 (1 when any bone attachment is live)
    BoneAttachChannel bone[3];   // +28 + 116*k, up to 3 attachments
    i32  frameStamp = 0;         // +64  = dword_62EB38 frame counter
};

// ===========================================================================
// Stock-stream record (a loaded .baf animation). The stock is a singly-linked
// list (head dword_13FC760, sentinel unk_13FC780, next at stream+352) keyed by
// a name string at the start of the record. We model the engine fields the
// attach path reads.
// ===========================================================================
struct AnimStream {
    std::string name;            // record start: the name key (StrCmpNoCase)
    i32  meshType   = 0;         // +320  compared to node+8
    i32  refCount   = 0;         // +332  ++ on attach
    i32  frameStamp = 0;         // +344  = dword_649D58
    i32  loDefault  = 0;         // +336  default low-frame
    i32  hiDefault  = 0;         // +340
    i32  frameTotal = 0;         // +328  total keyframes
    i32  poseTable  = 0;         // +348  pose-table base (192-byte stride)
    u8   normalsBuilt = 0;       // +360
    u8   noResetFrame = 0;       // +361
    // poseFrameCounts[i] = *(poseTable + 192*i + 4): per-pose keyframe count.
    std::vector<i32> poseFrameCounts;
};

// Hooks for the engine edges (inert defaults => pure logic reconstruction).
struct AnimObjHooks {
    // VIBE_Anim_BuildFrameTangents(rec): build Catmull-Rom tangents (object_anim.h).
    void (*buildFrameTangents)(ObjAnimRecord* rec) = nullptr;
    // VIBE_Anim_CalculateAnimNormals(stream, normalsSrc): build animated normals.
    void (*calculateAnimNormals)(AnimStream* stream, i32 normalsSrc) = nullptr;
    // VIBE_Anim_AssignSubMeshBones(drawData) / VIBE_Anim_ComputeBoneMatrices(drawData).
    void (*assignSubMeshBones)(i32 drawData) = nullptr;
    void (*computeBoneMatrices)(i32 drawData) = nullptr;
    // VIBE_ModelIo_LoadBinaryAnimation(path, key, loopFlag) -> stream (.baf
    // loader). `key` is the a2/edx name copied into the record (StrNCopyPad 63)
    // — the stock lookup key, DISTINCT from the I/O path.
    AnimStream* (*loadBinaryAnimation)(const char* path, const char* key,
                                       u8 loopFlag) = nullptr;
    // dword_62EB38 (global render frame counter) and dword_649D58 (anim epoch).
    i32 frameCounter = 0;   // dword_62EB38
    i32 animEpoch    = 0;   // dword_649D58
};

void SetAnimObjHooks(const AnimObjHooks* hooks);
const AnimObjHooks& GetAnimObjHooks();

// ---------------------------------------------------------------------------
// The anim stock list (g_animStock). FindFreeMeshSlot / LoadStreamToStock walk
// it. Exposed for the routines + tests. The original's head/sentinel are
// dword_13FC760 / unk_13FC780; we model the list directly.
// ---------------------------------------------------------------------------
std::vector<AnimStream*>& AnimStock();
void ResetAnimStock();

// ===========================================================================
// gilde.exe 0x5cf114 — VIBE_Anim_FindFreeMeshSlot.
// Walk the stock list; return the first stream whose name matches `name`
// (case-INSENSITIVE per the StrCmpNoCase thunk), or nullptr. The original
// returns 0 when the list is empty (head == sentinel).
// ===========================================================================
AnimStream* FindFreeMeshSlot(const char* name);

// ===========================================================================
// gilde.exe 0x5d3858 — VIBE_Anim_LoadStreamToStock (eax=name, bl=loopFlag,
// edx=key). Build "animations/" + `name` (the I/O path), but look the stock up
// by `key` (a3/edx — the short display key the callers build, e.g.
// "%s_%s"; see VIBE_Character_AttachMotion @0x403408: eax=full .baf path,
// edx=short key). Already-loaded => return it (logs the dup); else load the
// .baf via the model IO hook — which copies `key` into the record name — and
// PREPEND it to the stock list. Returns the stream, or nullptr on load fail.
// ===========================================================================
AnimStream* LoadStreamToStock(const char* name, u8 loopFlag, const char* key);

// ===========================================================================
// gilde.exe 0x5cef14 — VIBE_Anim_CreateObjectAnim (eax=node,edx=src,ebx=frameCount).
// Free any existing node->objAnim, then (only for class 3/4) allocate a new
// object-anim record, copy the source keyframe descriptor (qmemcpy), multiply
// each frame's duration field by 3, snapshot the node pose, set up the frame
// index (looped -> AdvanceFrameIndex from the last frame; else 0/1), build the
// tangents, and stamp the frame counter. Returns the new record (or nullptr).
// `srcFrames` is the source descriptor's keyframe array (the +56 block).
// ===========================================================================
ObjAnimRecord* CreateObjectAnim(AnimNode* node, i32 srcHandle, i32 frameCount,
                                const std::vector<i32>& srcFrames,
                                bool looped);

// ===========================================================================
// gilde.exe 0x5d0b64 — VIBE_Anim_AttachToBone (eax=model,ebx=ctrlWord).
// Find a free bone-channel slot (<3) on the model; require a matching stock
// stream (`name`), same mesh type, and a free channel. Initialize the channel
// (blend = 1.0, frame index per loop/non-loop), bump the stream refcount, build
// the stream's normals once, assign submesh bones + bone matrices. Returns the
// channel index used (0..2), or -1 on failure (the original returns the channel
// ptr / 0).
// ===========================================================================
i32 AttachToBone(AnimNode* model, const char* name, i32 ctrlWord);

// ===========================================================================
// gilde.exe 0x5d0d38 — VIBE_Anim_PruneExpiredAttachments (eax=model,edx=name).
// Drop every bone channel whose stream name equals `name` (case-SENSITIVE
// StrCmp), releasing its mesh data; recompute `boneActive` (clear when all 3
// slots empty), reassign submesh bones + bone matrices.
// ===========================================================================
void PruneExpiredAttachments(AnimNode* model, const char* name);

// ===========================================================================
// gilde.exe 0x5cec00 — VIBE_Anim_FreeObjAnimData (eax=node).
// Free node->objAnim (and its frame array) and clear node+464.
// ===========================================================================
void FreeObjAnimData(AnimNode* node);

}  // namespace guild::render
