#pragma once
#include "guild/common/types.h"
#include "render/node_lod.h"   // LodView (the live LOD-select globals the adapter reads)

// =============================================================================
// guild::render — VIBE_Mesh_AttachStockTextures (gilde.exe 0x5d1114).
//
// The LAST hop of the stock-object LOD attach: bind a loaded stock .bgf mesh into
// a renderable per-frame DRAW BLOCK. It is the leaf VIBE_Mesh_AttachStockObjectLods
// (0x5d1824, src/render/mesh_lod_name.*) reaches via the `attachStockTextures` hook.
//
//   0x5d1114  VIBE_Mesh_AttachStockTextures(node, drawBlock, lodArg, name)
//
// What it does (1:1 from the decompile):
//   1. stock = FindStockObject(name); null -> return null.
//   2. Draw-block (a2) header: a2[2]=stock.vertexCount(+68), a2[3]=stock.polyCount
//      (+76), a2[4]=stock, a2+380=0; AllocPolysAndPoints(a2, lodArg) (0x5b0c10).
//   3. Per-vertex loop (a2[2]+8 verts, 80-byte stride): draw vert +72 = stock
//      vertexArray(+64) + i*24; +76 = 0; +64 = +68 = -1.
//   4. Per-polygon loop (a2[3] polys; draw poly 40-byte/10-dword stride at a2[1];
//      stock poly 56-byte stride at stock+72): copy stock poly ptr into draw poly
//      +16; resolve the 3 vertex ptrs (drawVertBase + 80*idx), mark +76 |= 0x80;
//      resolve the MATERIAL via the texture-record (stockPoly+36 -> texRec) and set
//      the per-vertex +64/+68 colour/blend dwords + poly +38 bit3 (2-sided) + each
//      vertex +77 from texRec.
//   5. Texture-set build: a2[5] = alloc(4 * stock.matCount(+480)); per material
//      index, find a draw poly whose stockPoly+40 == that index, copy its resolved
//      texture handle (draw poly +20) into a2[5][mat] and IncrementRefCount(h,1);
//      missing -> "stock object %s, texture %s not found" log. Then the
//      "not all textures used/found" verification.
//   6. LOD bookkeeping (only when a2 != node+492 + 1396, i.e. not the "_s" slot):
//      ++node.drawData.lodCount(+2316); node+532=4; if node+460 (active LOD frame)
//      is 0 -> SelectLodFrame(node) (0x5adb6c); else scan the 3 LOD records for the
//      first valid one and set node+460.
//   7. ++stock.refcount(+476); return stock.
//
// MEMORY MODEL. The original works on opaque, fixed-offset record blocks. We keep
// the exact byte offsets (rule 1) by operating on raw `u8*` blocks, NOT on the
// parsed C++ Mesh/BgfModel records (those are a different representation; the
// stock-object draw layout here is the live engine layout). The genuine engine
// leaves are routed through MeshAttachHooks (inert defaults) so the body is
// golden-testable headless with a synthetic stock object + mock hooks.
//
// Draw-block (a2) dword layout recovered from 0x5d1114:
//   a2[0] (+0)   : draw vertex array base ptr  (allocated by AllocPolysAndPoints)
//   a2[1] (+4)   : draw polygon array base ptr (allocated by AllocPolysAndPoints)
//   a2[2] (+8)   : vertex count  (= stock+68)
//   a2[3] (+12)  : polygon count (= stock+76)
//   a2[4] (+16)  : stock object ptr
//   a2[5] (+20)  : texture-set array ptr (allocated here: 4 * stock matCount)
//   a2+380       : byte flag, cleared
//   a2+381       : byte flag, cleared
//
// Draw VERTEX record (80-byte stride):
//   +64 (dword)  : colour/blend dword 0   (set -1 init, then per-poly material)
//   +68 (dword)  : colour/blend dword 1   (set -1 init, then per-poly material)
//   +72 (dword)  : source ptr = stock vertexArray + i*24
//   +76 (byte)   : flags (0 init; |= 0x80 when referenced by a poly)
//   +77 (byte)   : per-vertex 2-sided byte (texRec+104 & 1)
//
// Draw POLYGON record (40 bytes / 10 dwords stride):
//   dword[0..2]  : the 3 resolved draw-vertex ptrs (drawVertBase + 80*idx)
//   +24/+28/+32  : float UV/offset (from v13[6]/[7]/[8]; copied/zeroed per decompile)
//   +36 (byte)   : flags36 (bit0 cleared)
//   +38 (byte)   : flags38 (bit3 = 2-sided from texRec+104&1)
//   dword[4] +16 : stock poly ptr (this poly's source 56-byte stock poly)
//   dword[5] +20 : resolved texture handle (texRec ptr, or 0 when matIndex < 0)
//
// Stock OBJECT record:
//   +64  (dword) : vertex array base (24-byte stock verts)
//   +68  (dword) : vertex count
//   +72  (dword) : polygon array base (56-byte stock polys)
//   +76  (dword) : polygon count
//   +476 (dword) : refcount (++ on attach)
//   +480 (dword) : material count
//   +516 (dword) : texture-name array base (+129*4; used in the not-found log)
//
// Stock POLYGON record (56-byte stride):
//   +24/+28/+32 (dword) : the 3 vertex indices (into the stock vertex array)
//   +36 (dword)         : RESOLVED texture-record index (-> texRec, << 7); < 0 = none
//   +40 (dword)         : MATERIAL index (0..matCount-1; the texture-set grouping key)
//
// Texture RECORD (dword_1406A84 base, 128-byte stride; NOT ours -> hook):
//   +104 (byte)  : flags; bit0 = 2-sided
//   +108 (byte)  : transparency byte (!= 0xFF => has alpha); the alpha level
//   +110 (byte)  : mode bits; bit0 = alpha (mode 1), bit1 = additive (mode 2)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// PER-FRAME DRAW-BLOCK LP64 LAYOUT CONTRACT (shared reconciliation, rule 1/3).
//
// The original draw block (a2) packs six 32-bit dwords a2[0..5]:
//   a2[0] +0  draw-vertex array ptr     a2[3] +12 polygon count
//   a2[1] +4  draw-polygon array ptr    a2[4] +16 STOCK-OBJECT ptr
//   a2[2] +8  vertex count              a2[5] +20 texture-SET array ptr
// On a 64-bit host the four pointer-bearing slots are 8 bytes, so a verbatim
// layout overlaps; AttachStockTextures (the WRITER) therefore relocates them to a
// self-consistent native layout (this is the layout it actually stores):
//   +0  vertArr(ptr8)  +8  polyArr(ptr8)  +16 vertCount(u32) +20 polyCount(u32)
//   +24 STOCK(ptr8)    +32 texSET(ptr8)   +380/+381 flag bytes
// VIBE_Object_AttachToUniverseNode (the READER, sim/object_lifecycle10.cpp) reads
// the engine's `*(v7 + 384*i + 260)` (== LOD-frame_base + a2[4] = the STOCK ptr,
// the resident-mesh gate) and `*(v7 + 384*i + 264)` (== a2[5] texSET array). Under
// the relocation those map to frame_base + kFrameStockSlot / + kFrameTexSetSlot.
// Both files include these constants so the live writer/reader agree byte-for-byte;
// the synthetic unit-test fixtures use them too. (LOD-frame_base = drawData + 244 +
// 384*i; the engine +260/+264 == frame_base+16/+20 before relocation.)
// ---------------------------------------------------------------------------
namespace frame {
constexpr int kLodFrameBase  = 244;   // drawData + 244 + 384*i  (LOD-frame base)
constexpr int kLodFrameStride = 384;  // 384-byte LOD-frame stride
constexpr int kVertArrPtr    = 0;     // a2[0] native ptr
constexpr int kPolyArrPtr    = 8;     // a2[1] native ptr
constexpr int kVertCount     = 16;    // a2[2] u32 (engine +8, relocated)
constexpr int kPolyCount     = 20;    // a2[3] u32 (engine +12, relocated)
constexpr int kStockSlot     = 24;    // a2[4] native ptr — STOCK ptr / resident gate
constexpr int kTexSetSlot    = 32;    // a2[5] native ptr — texture-SET array
}  // namespace frame

// ---------------------------------------------------------------------------
// Texture-record view (the +104/+108/+110 fields of a 128-byte dword_1406A84
// record). The handle is the record's identity (the original stored the record
// ptr itself as the draw-poly's resolved texture handle). `present` is false when
// the matIndex resolved to no record (used only for tests; the original always has
// a record when matIndex >= 0).
// ---------------------------------------------------------------------------
struct AttachTextureRecord {
    u8       flags104 = 0;   // texRec+104 (bit0 = 2-sided)
    u8       trans108 = 0;   // texRec+108 (transparency byte; != 0xFF => alpha)
    u8       mode110  = 0;   // texRec+110 (bit0 = alpha mode 1, bit1 = additive)
    uintptr_t handle  = 0;   // the resolved texture handle stored in draw poly +20
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in mesh_attach_textures.cpp). Each maps a
// genuine engine leaf the original calls; the offsets the body reads/writes on the
// returned/passed blocks are the engine offsets cited above.
// ---------------------------------------------------------------------------
struct MeshAttachHooks {
    // VIBE_Mesh_FindStockObject (0x5d10d0): the registry lookup by name. Returns the
    // raw stock-object block ptr, or null when not found. Default: null (no asset).
    u8* (*findStockObject)(const char* name) = nullptr;

    // VIBE_Object_AllocPolysAndPoints (0x5b0c10): given the draw block (a2[2]=vertex
    // count, a2[3]=poly count already set), (re)allocate a2[1] = 40 * polyCount draw
    // polys and a2[0] = 80 * (vertexCount + 8) draw verts. Default: a no-op (the body
    // then no-ops the loops because a2[0]/a2[1] stay null — tests install a real one).
    void (*allocPolysAndPoints)(u8* drawBlock, int lodArg) = nullptr;

    // The texture-record accessor: matIndex (stockPoly+36, >= 0) -> the 128-byte
    // record's +104/+108/+110 fields + the handle stored as the resolved texture.
    // Default: returns a zeroed record (opaque, no alpha, handle = a synthetic value
    // derived from matIndex so the texture-set build still has a non-null handle).
    AttachTextureRecord (*textureRecord)(int matIndex) = nullptr;

    // VIBE_Texture_IncrementRefCount (0x5da2e4): bump the texture's refcount.
    // Default: no-op.
    void (*textureIncrementRefCount)(uintptr_t handle, int delta) = nullptr;

    // VIBE_Mesh_SelectLodFrame (0x5adb6c): pick the active LOD frame ptr for node.
    // Returns the chosen drawData+244+384*k frame block address (as an opaque value
    // written into node+460), or 0. Default: returns 0 (forces the scan fallback).
    uintptr_t (*selectLodFrame)(u8* node) = nullptr;

    // Error log for the "stock object %s, texture %s not found" / "Not all Textures
    // used/found!" paths. Default: no-op. `texName` may be null.
    void (*logError)(const char* fmt, const char* stockName, const char* texName) = nullptr;
};

MeshAttachHooks&       MeshAttachHooksMut();
const MeshAttachHooks& AttachHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1114 — VIBE_Mesh_AttachStockTextures.
//   node       : the object node (a1). Used only for the LOD bookkeeping (step 6):
//                +460 active-frame, +492 drawData, +532, drawData+2316 lodCount.
//   drawBlock  : the per-frame draw block (a2) to fill. Byte ptr; the 6 header
//                dwords + the +380/+381 flag bytes live at the offsets above.
//   lodArg     : the per-LOD argument threaded to AllocPolysAndPoints (a3).
//   name       : the stock object's name (a4) for FindStockObject + the error log.
// Returns the stock object ptr (the original's eax = v75), or null if not found.
// ---------------------------------------------------------------------------
u8* AttachStockTextures(u8* node, u8* drawBlock, int lodArg, const char* name);

// ---------------------------------------------------------------------------
// Wire AttachStockTextures as the real implementation of the MeshLodHooks
// `attachStockTextures` hook that VIBE_Mesh_AttachStockObjectLods (0x5d1824) calls.
// The adapter recovers the node's drawData (node+492) and forwards to the body
// (the original's a2 == drawData + drawBlockOffset). Call once at engine init (the
// stock-object render path); idempotent.
// ---------------------------------------------------------------------------
void InstallStockTextureAttach();

// ---------------------------------------------------------------------------
// gilde.exe 0x5adb6c — VIBE_Mesh_SelectLodFrame, raw-object-block adapter.
//   Reads the node's LOD fields (drawData@+492, lodCount@+2316, pos@+76.., render
//   flags@+531, active frame@+460), runs render::SelectLodFrame (node_lod.cpp), sets
//   node+528 |= 0x40 when the chosen frame changed, and returns the selected LOD-frame
//   block address (drawData+244+384*idx), or 0 when there is no drawable LOD / the
//   chosen frame is empty (== the original's `return 0`). Installed as the
//   MeshAttachHooks::selectLodFrame leaf by InstallStockTextureAttach.
// ---------------------------------------------------------------------------
uintptr_t SelectLodFrameForNode(u8* node);

// Supply the live LOD-select view (current camera present/pos, fov scale, force-rebuild
// flag) the distance branch reads — the engine's dword_13FCD1C / flt_13FC774 /
// byte_64A068 process globals. Defaults to "no active camera" (forced-LOD branch).
void SetLodSelectView(const LodView& view);

} // namespace guild::render
