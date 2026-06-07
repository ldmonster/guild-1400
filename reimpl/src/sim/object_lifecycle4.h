#pragma once
// ===========================================================================
// object_lifecycle4.{h,cpp} — VIBE_Object_* heap scene-graph node spawn /
// teardown / scene-link / draw-data (sub-mesh) lifecycle (batch 4).
// namespace guild::sim.
// ===========================================================================
// MODULE: the remaining untranslated VIBE_Object_* leaves that allocate, link,
// and free the heap "d3:SpawnObject" node (the ~0x21C-byte block modeled as
// SceneObject / SceneNode3 in batches 2/3) and its attached draw-data /
// sub-mesh sub-allocations. batches 1/2/3 covered the gameplay-RECORD, the
// flag/fill mutators, the transform-setter family and the find/classify walks;
// this batch covers:
//
//   * VIBE_Object_Spawn — allocate the 0x21C node, classify the node-TYPE byte
//     (+533) from the spawn-name first char ('p'->7, 'r'->6, 's'->8 else 5,
//     light-info >=5), copy the 64-byte name, attach the universe link.
//   * VIBE_Object_DisposeResources / VIBE_Object_FreeDrawData /
//     VIBE_Object_FreeSubMeshData / VIBE_Object_AllocPolysAndPoints /
//     VIBE_Object_InitSubMeshEntry — the draw-data sub-mesh (de)allocation.
//   * VIBE_Object_LinkIntoScene / _UnlinkFromScene / _UnlinkFromList — the
//     intrusive prev/next/parent/firstChild scene-list maintenance.
//   * VIBE_Object_ChangeTransparencySubMeshes / _ApplyTransparencyTree — the
//     per-sub-mesh transparency fan-out + its subtree walk wrapper.
//   * VIBE_Object_SetLowNibbleFlag — the floor low-nibble flag swap.
//   * VIBE_Object_MarkState2 / _ResetState / _ResetStateAlt — tiny command
//     thunks.
//
// Every cross-module callee that is NOT reconstructed (the debug heap
// alloc/free, InitStruct, StrNCopyPad, the scene-graph walker, the transparency
// applicator, the light/shadow/texture/anim/floor leaves, the universe slot
// switch and the build/light state thunks) is routed through ObjLife4Hooks
// (inert defaults installed by THIS .cpp) so the node-field arithmetic, the
// node-type classification and the list pointer surgery stay exact and
// independently testable. Tests install captors.
//
// The node is the SAME 0x21C block modeled elsewhere; to stay 1:1 with these
// functions' raw dword/byte index access (*(a1+520), *((_DWORD*)a1+122), ...)
// without colliding with the named SceneObject/SceneNode3 views, it is modeled
// here as ObjNode4 — a raw byte buffer with typed accessors. Offsets on every
// touched field.
//
// Translated functions (gilde.exe / imagebase 0x400000):
//   0x5b054c  VIBE_Object_Spawn                    (alloc 0x21C, classify type)
//   0x5b0660  VIBE_Object_DisposeResources         (free draw-data + caches)
//   0x5b0a20  VIBE_Object_LinkIntoScene            (link node into scene list)
//   0x5b0adc  VIBE_Object_UnlinkFromScene          (unlink from scene list)
//   0x5b23ec  VIBE_Object_UnlinkFromList           (unlink from sibling chain)
//   0x5b0e18  VIBE_Object_InitSubMeshEntry         (zero a sub-mesh entry)
//   0x5b0da0  VIBE_Object_FreeDrawData             (free all sub-meshes + block)
//   0x5b0c8c  VIBE_Object_FreeSubMeshData          (free one sub-mesh's buffers)
//   0x5b0c10  VIBE_Object_AllocPolysAndPoints      (realloc polys/points buffers)
//   0x5b2964  VIBE_Object_ChangeTransparencySubMeshes (per-sub-mesh fan-out)
//   0x5b29b0  VIBE_Object_ApplyTransparencyTree    (subtree transparency walk)
//   0x5c4634  VIBE_Object_SetLowNibbleFlag         (floor low-nibble swap)
//   0x567520  VIBE_Object_MarkState2               (build-op-90 thunk)
//   0x538400  VIBE_Object_ResetState               (light gray-color thunk)
//   0x53841c  VIBE_Object_ResetStateAlt            (light gray-color thunk, dup)
#include <cstdint>

#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// ObjNode4 — the "d3:SpawnObject" node (gilde.exe alloc size 0x21C = 540 B).
// Named members at the recovered BYTE offsets (in the comments) the spawn /
// link / free family touches. The original is 32-bit, so it packs 4-byte
// pointers at 4-byte-spaced offsets (e.g. +496/+500/+504/+508); we use native
// pointers and let the compiler lay them out non-overlapping (the same
// reconstruct-by-value approach as guild::sim::SceneObject — the byte offsets
// are provenance, not a binary-faithful layout). Scalar fields keep their
// recovered offsets in the comments.
// ===========================================================================
struct ObjNode4 {
    char  name[64];        // +0x00  (0)    name (first char classifies type)
    void* meshFrame;       // +0x1CC (460)  selected LOD frame (FreeDrawData -> 0)
    void* animData;        // +0x1D0 (464)
    void* miscBlock;       // +0x1D4 (468)
    void* lightInfo;       // +0x1E8 (488)  *((_DWORD*)a1+122) (alloc'd type>=5)
    void* drawData;        // +0x1EC (492)  draw-data block base
    void* prevList;        // +0x1F0 (496)  intrusive list prev
    void* nextList;        // +0x1F4 (500)  intrusive list next
    void* parent;          // +0x1F8 (504)
    void* firstChild;      // +0x1FC (508)
    void* universe;        // +0x208 (520)  off_649D64
    void* sound3d;         // +0x20C (524)
    u8    flags528;        // +0x210 (528)  bit0=parentless,bit1=linked,bit2=built
    u8    flags529;        // +0x211 (529)  bit2 = shadow caster owner
    u8    nodeType;        // +0x215 (533)  (0..8)
    u8    nodeTypeShadow;  // +0x216 (534)  copy of nodeType

    void reset() {
        for (auto& c : name) c = 0;
        meshFrame = animData = miscBlock = lightInfo = drawData = nullptr;
        prevList = nextList = parent = firstChild = universe = sound3d = nullptr;
        flags528 = flags529 = nodeType = nodeTypeShadow = 0;
    }
    ObjNode4() { reset(); }
};

// ===========================================================================
// SubMeshEntry — one entry inside the draw-data block (the original strides
// these at 384 bytes from drawData+244). The (de)alloc family reads:
//   +0   points      (ptr)        +4   polys       (ptr)
//   +8   polyCount   (i32)        +12  pointCount  (i32)
//   +16  mesh        (ptr)        +20  texArray    (ptr)
//   +24  parentDraw  (ptr)        +376 tag byte (0xFF when uninitialised)
//   +378 flagsByte (bit0)         +132..  three LOD slots (stride 116)
// Same reconstruct-by-value note as ObjNode4 (native pointers, compiler layout;
// the byte offsets are provenance). The LOD slots are modeled as `lod[3]`.
// ===========================================================================
struct SubMeshLod {
    void* meshData;   // +132 within the entry for lod[0] (stride 116)
    void* extraPtr;   // +20 of the slot (cleared by InitSubMeshEntry)
    i32   field32;    // +32 of the slot (entry-84 in the loop's cursor view)
    i32   field36;    // +36
    i32   field28;    // +28
    u8    slotFlags;  // +22 of the slot (bit1 cleared)
    SubMeshLod() : meshData(nullptr), extraPtr(nullptr),
                   field32(0), field36(0), field28(0), slotFlags(0) {}
};
struct SubMeshEntry {
    void* points;       // +0
    void* polys;        // +4
    i32   polyCount;    // +8
    i32   pointCount;   // +12
    void* mesh;         // +16
    void* texArray;     // +20
    void* parentDraw;   // +24
    u8    tag;          // +376 (0xFF when init)
    u8    flagsByte;    // +378 (bit0)
    SubMeshLod lod[3];
    SubMeshEntry()
        : points(nullptr), polys(nullptr), polyCount(0), pointCount(0),
          mesh(nullptr), texArray(nullptr), parentDraw(nullptr),
          tag(0), flagsByte(0) {}
};

// ===========================================================================
// DrawData — the +492 block. The family reads a sub-mesh count at +2316 and
// strides SubMeshEntry at 384 from base+244, plus a trailing entry at +1396.
// Plus the +2304/+2308/+2312 "scratch geometry" sub-block (DisposeResources).
// Modeled as a count + a sub-mesh vector + the trailing entry + the scratch
// triple. (`base+244+384*i` -> subMeshes[i]; `base+1396` -> trailing.)
// ===========================================================================
struct DrawData {
    u8 subMeshCount = 0;     // +2316 (byte count)
    void* scratch0 = nullptr;   // +2304
    void* scratch1 = nullptr;   // +2308
    i32   scratchValid = 0;     // +2312 (non-zero => free scratch0/1)
    SubMeshEntry* subMeshes = nullptr;  // base+244, stride 384
    SubMeshEntry* trailing  = nullptr;  // base+1396
};

// Alloc sizes / strings the originals pass.
constexpr int kNodeAllocSize = 0x21C;  // "d3:SpawnObject"
constexpr int kLightInfoSize = 0x1AC;  // "d3:SpawnObject(lightinfo)"

// ---------------------------------------------------------------------------
// Mockable hooks for the unreconstructed leaves the originals call. nullptr =>
// inert (the node-field/list arithmetic still runs faithfully). Tests install
// captors to assert ordering / allocation sizes / arguments.
// ---------------------------------------------------------------------------
struct ObjLife4Hooks {
    // VIBE_Memory_AllocDebug(size, tag) — debug heap alloc. Returns block ptr.
    void* (*allocDebug)(int size, const char* tag) = nullptr;
    // VIBE_Memory_FreeDebug(ptr, ...) — debug heap free.
    void  (*freeDebug)(void* ptr) = nullptr;
    // VIBE_Object_InitStruct(node) — zero/initialise the freshly-alloc'd node.
    void  (*initStruct)(ObjNode4* node) = nullptr;
    // VIBE_Util_StrNCopyPad(dst, src, n) — copy name with zero pad to n bytes.
    void  (*strNCopyPad)(char* dst, const char* src, int n) = nullptr;
    // VIBE_Object_ChangeTransparency(node, subMesh, alpha, idx) — per-mesh alpha.
    void  (*changeTransparency)(ObjNode4* node, SubMeshEntry* subMesh, int alpha,
                                int idx) = nullptr;
    // VIBE_SceneGraph_WalkAndInvoke(universe, node, cb, mask, arg) — subtree walk.
    int   (*walkAndInvoke)(void* node, int mask, int arg) = nullptr;
    // VIBE_Texture_ReleaseEntry(tex).
    void  (*textureRelease)(void* tex) = nullptr;
    // VIBE_Anim_ReleaseMeshData(meshData).
    void  (*animReleaseMesh)(void* meshData) = nullptr;
    // VIBE_Light_RemoveCacheEntry(node) / VIBE_Shadow_* / scene-graph removers.
    void  (*lightRemoveCache)(ObjNode4* node) = nullptr;
    void  (*shadowRemoveByLight)(ObjNode4* node) = nullptr;
    void  (*shadowClearAll)(ObjNode4* node) = nullptr;
    // VIBE_Object_InvalidateCurrent(arg) / VIBE_Object_SetWorldTranslation —
    // the LinkIntoScene "make current" path.
    void  (*invalidateCurrent)(u8 arg) = nullptr;
    void  (*setWorldTranslation)(ObjNode4* node) = nullptr;
    // VIBE_Floor_FreeTileBuffers / _AllocInflateBuffers / _BuildTilePolys.
    void  (*floorFreeTiles)(ObjNode4* node) = nullptr;
    void  (*floorAllocInflate)(ObjNode4* node) = nullptr;
    void  (*floorBuildPolys)(ObjNode4* node) = nullptr;
    // Tiny command thunks: VIBE_Command_RequestBuildOp90_Thunk(node, op),
    // VIBE_Light_SetGrayColorThunk(a, b).
    void  (*requestBuildOp)(ObjNode4* node, int op) = nullptr;
    void  (*lightSetGray)(int a, int b) = nullptr;
};
void ObjLife4SetHooks(const ObjLife4Hooks& hooks);
void ObjLife4ResetHooks();

// ---------------------------------------------------------------------------
// Module globals (gilde.exe), owned here; faithful to the originals. The "make
// current" global the LinkIntoScene path tests (dword_13FCD1C), the scene list
// head (dword_13FD140) and the list-sentinel node (unk_13FCF4C) and the active
// universe (off_649D64). Tests can set/read them.
// ---------------------------------------------------------------------------
extern void* g_objCurrent4;     // dword_13FCD1C
extern void* g_sceneListHead;   // dword_13FD140
extern void* g_sceneListSentinel; // &unk_13FCF4C
extern void* g_activeUniverse;  // off_649D64
extern bool  g_lightInfoForce5; // byte_649D54 (forces type 6 -> 5)

// ---------------------------------------------------------------------------
// 0x5b054c — VIBE_Object_Spawn. al = kind@al, edx = name@edx. Allocates the
//   node, classifies node-type from the name first char, copies the name and
//   attaches the active universe. Returns the node.
//     kind<5  -> nodeType = kind
//     kind>=5 -> alloc lightInfo block; first char 'p'(112)->7, 'r'(114)->6,
//                's'(115)->8, else 5; if g_lightInfoForce5 && type==6 -> 5.
//     type==6 -> *(lightInfo+404) = 0;  type==8 -> flags529 |= 4.
//     nodeTypeShadow = nodeType;  StrNCopyPad(node, name, 64);  universe set.
// ---------------------------------------------------------------------------
ObjNode4* ObjectSpawn(u8 kind, const char* name);

// 0x5b0660 — VIBE_Object_DisposeResources. Free draw-data + light/shadow caches
//   for `node`. Returns node (or 0 if null).
int ObjectDisposeResources(ObjNode4* node);

// 0x5b0a20 — VIBE_Object_LinkIntoScene. Make-current (type 3) + intrusive link
//   at the scene-list head. Returns node.
ObjNode4* ObjectLinkIntoScene(ObjNode4* node);

// 0x5b0adc — VIBE_Object_UnlinkFromScene. Splice the node out of the prev/next
//   scene list; clear flags528 bit1. type 3 clears g_objCurrent4. Returns node.
ObjNode4* ObjectUnlinkFromScene(ObjNode4* node);

// 0x5b23ec — VIBE_Object_UnlinkFromList. Splice the node out of its sibling
//   chain (prev/next/parent.firstChild). Clears parent/prev/next. Returns node.
ObjNode4* ObjectUnlinkFromList(ObjNode4* node);

// 0x5b0e18 — VIBE_Object_InitSubMeshEntry. Zero the sub-mesh entry; set
//   parentDraw(+24), tag(+376) = 0xFF, clear the 3 LOD slots (stride 116, 3
//   iterations) and their extraPtr(+20)/field28/32/36 & slotFlags bit1.
void ObjectInitSubMeshEntry(SubMeshEntry* entry, void* parentDraw);

// 0x5b0da0 — VIBE_Object_FreeDrawData. For each of the subMeshCount sub-meshes
//   call FreeSubMeshData, plus the trailing entry, clear count, free the
//   draw-data block, clear node.drawData and node.meshFrame. Returns 1.
int ObjectFreeDrawData(ObjNode4* node);

// 0x5b0c8c — VIBE_Object_FreeSubMeshData. Reset transparency (tag!=0xFF or
//   flagsByte bit0), free polys(polyCount>0) / points(pointCount>0) /
//   texArray (releasing each texture) and the mesh, release the 3 LOD
//   meshData ptrs. Returns 0.
int ObjectFreeSubMeshData(SubMeshEntry* entry);

// 0x5b0c10 — VIBE_Object_AllocPolysAndPoints. If polyCount && pointCount: free
//   old polys realloc 40*pointCount, free old points realloc 80*(polyCount+8).
//   Returns the points block.
void* ObjectAllocPolysAndPoints(SubMeshEntry* entry);

// 0x5b2964 — VIBE_Object_ChangeTransparencySubMeshes. For each sub-mesh
//   (+2316 count, stride 384) call changeTransparency(node, sub, *alpha, ++i).
//   Returns 1.
char ObjectChangeTransparencySubMeshes(ObjNode4* node, const int* alpha);

// 0x5b29b0 — VIBE_Object_ApplyTransparencyTree. If node, walk the subtree
//   invoking ChangeTransparencySubMeshes (mask 576, arg = alpha). Returns walk
//   result (lo byte) or 0.
char ObjectApplyTransparencyTree(ObjNode4* node, int alpha);

// 0x5c4634 — VIBE_Object_SetLowNibbleFlag. If the low nibble of +7281 differs
//   from `newNibble`: free tile buffers, write the new low nibble, alloc
//   inflate buffers; if `rebuild`, build tile polys. `tileByte` is the current
//   *(node+7281). Returns the resulting *(node+7281) (full byte).
//   (Modeled on a caller-supplied byte to stay 1:1 with the +7281 access that
//   is outside the 540-byte node block.)
u8 ObjectSetLowNibbleFlag(u8* tileByte, u8 newNibble, bool rebuild,
                          ObjNode4* node);

// 0x567520 — VIBE_Object_MarkState2. requestBuildOp(node, 2). Returns 1.
int ObjectMarkState2(ObjNode4* node);
// 0x538400 — VIBE_Object_ResetState. lightSetGray(0, 4608). Returns 1.
int ObjectResetState(ObjNode4* node);
// 0x53841c — VIBE_Object_ResetStateAlt. byte-identical to ResetState. Returns 1.
int ObjectResetStateAlt(ObjNode4* node);

}  // namespace guild::sim
