#include "render/mesh_attach_textures.h"
#include "render/mesh_lod_name.h"
#include "render/node_lod.h"        // render::SelectLodFrame (the 0x5adb6c math)

#include <cstdint>
#include <cstring>
#include <vector>

// =============================================================================
// guild::render — VIBE_Mesh_AttachStockTextures (gilde.exe 0x5d1114). See
// mesh_attach_textures.h for the full record/offset map. The body below is a
// 1:1 translation of the Hex-Rays decompile; every offset is cited inline.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Hooks (inert defaults). The defaults keep the body inert/headless: no stock
// object found, no alloc, opaque texture records, no refcount/log side effects.
// ---------------------------------------------------------------------------
MeshAttachHooks& MeshAttachHooksMut() {
    static MeshAttachHooks g;
    return g;
}
const MeshAttachHooks& AttachHooks() { return MeshAttachHooksMut(); }

// ---------------------------------------------------------------------------
// Raw little-endian field accessors on the opaque engine blocks. The original
// is 32-bit x86; pointers in a draw block are stored as machine words. In this
// reconstruction the draw block is a native byte buffer and "ptr" fields hold
// native uintptr_t values (the tests round-trip them as such). Dword fields that
// hold counts/indices/handles use u32 stores to match the original int writes.
// ---------------------------------------------------------------------------
static inline u32  rd32(const u8* p, int off) {
    u32 v; std::memcpy(&v, p + off, 4); return v;
}
static inline void wr32(u8* p, int off, u32 v) { std::memcpy(p + off, &v, 4); }
static inline uintptr_t rdptr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}
static inline void wrptr(u8* p, int off, uintptr_t v) {
    std::memcpy(p + off, &v, sizeof(v));
}
static inline u8   rd8(const u8* p, int off) { return p[off]; }
static inline void wr8(u8* p, int off, u8 v) { p[off] = v; }

// In the original the draw block stores raw 32-bit pointers in dwords [0],[1],[4],
// [5]. Because native pointers are 64-bit here, those header slots and the per-poly
// vertex/stock/texture ptr slots are stored as native uintptr_t. We therefore index
// pointer-bearing fields by their LOGICAL slot, not the original byte offset, so the
// layout stays self-consistent under 64-bit while the NON-pointer offsets (+64/+68/
// +72/+76/+77 on verts; +36/+38/+24.. on polys; the stock-object/texRec reads) keep
// their exact engine byte offsets. The draw-vertex stride is 80 and the draw-poly
// stride is 40 in the original; here we expose them as named constants and store
// pointer fields at slot offsets within those strides. Tests allocate blocks sized
// to these strides.
//
// Draw vertex layout used here (matches the engine where it can):
//   slot ptr  +PTR_SRC : source stock-vertex ptr        (engine +72)
//   dword     +64,+68  : colour/blend dwords             (engine +64/+68)
//   byte      +76,+77  : flag bytes                       (engine +76/+77)
// Draw poly layout used here:
//   slot ptr  +0,+8,+16: the 3 resolved draw-vertex ptrs (engine dword[0..2])
//   slot ptr  +PP_STOCK: stock poly ptr                  (engine dword[4]/+16)
//   slot ptr  +PP_TEX  : resolved texture handle         (engine dword[5]/+20)
//   dword     +24/+28/+32 : UV floats                    (engine +24/+28/+32)
//   byte      +36,+38  : flag bytes                       (engine +36/+38)
//
// To avoid pointer-width hazards we lay verts at an 80-byte stride and polys at a
// 64-byte stride (>= the engine 40 to fit three 8-byte ptrs + two 8-byte ptrs).
// The byte-offset fields (+64/+68/+76/+77 vert; +36/+38 poly) still land at their
// engine offsets inside the stride. Vertex ptr is stored just past the engine fields.
namespace layout {
    constexpr int kVertStride = 80;     // engine draw-vertex stride
    constexpr int kVertSrcPtr = 48;     // native ptr slot (engine +72 dword); fits in stride
    // engine vert byte fields:
    constexpr int kVertCol0   = 64;
    constexpr int kVertCol1   = 68;
    constexpr int kVertFlag76 = 76;
    constexpr int kVertSide77 = 77;

    constexpr int kPolyStride = 64;     // >= engine 40, holds five native ptrs + fields
    constexpr int kPolyV0     = 40;     // three resolved draw-vertex ptr slots
    constexpr int kPolyV1     = 48;
    constexpr int kPolyV2     = 56;
    // engine poly fields kept at engine byte offsets within the stride:
    constexpr int kPolyUV0    = 24;     // engine +24 (v13[6] float)
    constexpr int kPolyUV1    = 28;     // engine +28 (v13[7])
    constexpr int kPolyUV2    = 32;     // engine +32 (v13[8])
    constexpr int kPolyFlag36 = 36;
    constexpr int kPolyFlag38 = 38;
    constexpr int kPolyStock  = 0;      // stock poly ptr  (engine dword[4]/+16): slot 0..7
    constexpr int kPolyTex    = 8;      // resolved tex handle (engine dword[5]/+20): slot 8..15
}

// Draw-block header slots (a2). The original packs six 32-bit fields; we keep counts
// as u32 at byte offsets 8/12 and pointer-bearing fields at native-ptr slots.
namespace hdr {
    constexpr int kVertArrPtr = 0;      // a2[0]  draw vertex array base (native ptr)
    constexpr int kPolyArrPtr = 8;      // a2[1]  draw polygon array base (native ptr)
    constexpr int kVertCount  = 16;     // a2[2]  vertex count (u32)
    constexpr int kPolyCount  = 20;     // a2[3]  polygon count (u32)
    constexpr int kStockPtr   = 24;     // a2[4]  stock object ptr (native ptr)
    constexpr int kTexSetPtr  = 32;     // a2[5]  texture-set array base (native ptr)
    constexpr int kFlag380    = 380;    // a2+380 byte
    constexpr int kFlag381    = 381;    // a2+381 byte
    constexpr int kHeaderSize = 384;    // header bytes the tests allocate before counts use it
}

// Stock object layout. The ORIGINAL byte offsets are documented per field, but the
// pointer-bearing fields are 4-byte dwords in the 32-bit original and would OVERLAP
// their adjacent count dword at native (8-byte) pointer width. So this native
// reconstruction places the three pointer fields at dedicated 8-byte slots in a
// reserved region past the engine fields (offsets >= 540, beyond +516+4), while the
// non-pointer count/refcount fields keep their exact engine byte offsets. Both the
// body and the tests use these named slots; the stock block is opaque (hook/loader
// constructed), so this is a private layout contract, not a serialised format.
namespace stock {
    // Original engine offsets (documented):
    //   +64  vertex array ptr   +68  vertex count
    //   +72  poly array ptr     +76  poly count
    //   +476 refcount           +480 material count
    //   +516 texture-name array ptr (+129*4)
    constexpr int kVertCount  = 68;     // +68 dword (engine offset kept)
    constexpr int kPolyCount  = 76;     // +76 dword (engine offset kept)
    constexpr int kRefCount   = 476;    // +476 dword (engine offset kept)
    constexpr int kMatCount   = 480;    // +480 dword (engine offset kept)
    // Pointer slots (native-width; reserved region — original was +64/+72/+516):
    constexpr int kVertArray  = 540;    // ptr (orig +64)
    constexpr int kPolyArray  = 548;    // ptr (orig +72)
    constexpr int kTexNameArr = 556;    // ptr (orig +516)
    constexpr int kStockVertStride = 24;
    constexpr int kStockPolyStride = 56;
}
// Stock polygon byte offsets (56-byte stride).
namespace spoly {
    constexpr int kIdx0    = 24;        // +24/+28/+32 vertex indices
    constexpr int kTexIdx  = 36;        // +36 resolved texture-record index (< 0 = none)
    constexpr int kMatIdx  = 40;        // +40 material index (texture-set grouping key)
}

// Object NODE byte offsets (a1/v74) for the LOD bookkeeping (step 6).
//   node+460  : active LOD-frame ptr (0 => select)
//   node+492  : drawData block ptr
//   node+532  : byte set to 4 on attach
//   drawData+1396 : the "_s" variant draw block ("not LOD" slot)
//   drawData+2316 : LOD-frame count (byte, incremented)
//   drawData + off + 244 : LOD record k (off = 384*k, k < 3)
//   record subfields: +252 (rec+8), +256 (rec+12), +260 (rec+16)
constexpr int kNodeActiveFrame  = 460;
constexpr int kNodeFlags528     = 528;   // node+528 flags byte; bit6 (0x40) = LOD dirty
constexpr int kNodeRenderFlags  = 531;   // node+531 (bits 0x30 = forced-LOD selector)
constexpr int kNodeDrawData     = 492;
constexpr int kNodeByte532      = 532;
constexpr int kNodePosX         = 76;    // node+76/+80/+84 world position floats
constexpr int kSVariantOffset   = 1396;
constexpr int kDrawDataLodCount = 2316;
constexpr int kLodRecordBase    = 244;

// ---------------------------------------------------------------------------
// gilde.exe 0x5adb6c — VIBE_Mesh_SelectLodFrame, raw-object-block adapter.
//
// The clean math reconstruction lives in node_lod.cpp (render::SelectLodFrame on a
// LodObject -> LOD index). The original is __usercall(eax=node) and the callers
// (AttachStockTextures step 6 below, and the per-frame node walk) expect back the
// chosen LOD-frame ADDRESS (drawData+244+384*idx) to store into node+460, with
// node+528 |= 0x40 set when the frame changed. This adapter reads the raw node
// fields into a LodObject, runs the reconstruction, applies the +528 dirty bit, and
// returns the frame address (0 on "no drawable LOD"/empty frame == the original's
// `return 0`). The LOD-frame header counts it validates (the original's result+8 /
// result+12 == a2[2] vertCount / a2[3] polyCount) live at the relocated
// hdr::kVertCount / hdr::kPolyCount under the 64-bit draw-block layout.
//
// The distance branch reads three process globals (dword_13FCD1C current camera,
// flt_13FC774 fov scale, byte_64A068 force-rebuild) that belong to the live universe
// runtime. Until it supplies them via SetLodSelectView, the view models "no active
// camera" (worldPresent=false), so SelectLodFrame takes the forced-LOD branch
// exactly as the original does when dword_13FCD1C == 0 (rule 8: named, not faked).
// ---------------------------------------------------------------------------
namespace {
LodView g_lodSelectView;   // default worldPresent=false -> forced-LOD branch (no camera)
}  // namespace

void SetLodSelectView(const LodView& v) { g_lodSelectView = v; }

uintptr_t SelectLodFrameForNode(u8* node) {
    if (!node)
        return 0;
    u8* drawData = reinterpret_cast<u8*>(rdptr(node, kNodeDrawData));   // node+492
    if (!drawData)
        return 0;
    const int lodCount = rd8(drawData, kDrawDataLodCount);             // drawData+2316
    if (lodCount <= 0)
        return 0;

    // The LOD frames are 384-byte draw blocks at drawData+244+384*k. Validate each
    // with the two header count dwords the original checks (result+8 == a2[2] vertCount
    // == relocated +kVertCount; result+12 == a2[3] polyCount == relocated +kPolyCount).
    u8* const frameBase = drawData + kLodRecordBase;                    // drawData+244
    std::vector<LodFrame> frames(static_cast<std::size_t>(lodCount));
    for (int k = 0; k < lodCount; ++k) {
        const u8* fr = frameBase + static_cast<std::size_t>(k) * frame::kLodFrameStride;
        frames[k].polyCount = static_cast<i32>(rd32(fr, frame::kVertCount));  // result+8
        frames[k].polyCap   = static_cast<i32>(rd32(fr, frame::kPolyCount));  // result+12
    }

    LodObject obj;
    u32 px = rd32(node, kNodePosX + 0), py = rd32(node, kNodePosX + 4),
        pz = rd32(node, kNodePosX + 8);
    std::memcpy(&obj.pos[0], &px, 4);
    std::memcpy(&obj.pos[1], &py, 4);
    std::memcpy(&obj.pos[2], &pz, 4);
    obj.renderFlags   = rd8(node, kNodeRenderFlags);                   // node+531
    obj.lodCount      = static_cast<u8>(lodCount);
    obj.drawDataReady = true;
    obj.frames        = frames.data();
    // node+460 is the active-frame ptr; recover its index so the dirty-bit "changed"
    // test matches the original's `result != *(node+460)`.
    const uintptr_t cur = rdptr(node, kNodeActiveFrame);              // node+460
    obj.currentFrameIndex =
        cur ? static_cast<i32>((cur - reinterpret_cast<uintptr_t>(frameBase)) /
                               frame::kLodFrameStride)
            : -1;

    bool dirty = false;
    const i32 idx = SelectLodFrame(obj, g_lodSelectView, &dirty);
    if (idx < 0)
        return 0;                                                     // original return 0
    if (dirty)
        wr8(node, kNodeFlags528, static_cast<u8>(rd8(node, kNodeFlags528) | 0x40));
    return reinterpret_cast<uintptr_t>(frameBase +
                                       static_cast<std::size_t>(idx) *
                                           frame::kLodFrameStride);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d1114 — VIBE_Mesh_AttachStockTextures.
// ---------------------------------------------------------------------------
u8* AttachStockTextures(u8* node, u8* drawBlock, int lodArg, const char* name) {
    const MeshAttachHooks& h = AttachHooks();

    // 1. stock = FindStockObject(name); null -> return null. (0x5d1126)
    u8* stock = h.findStockObject ? h.findStockObject(name) : nullptr;
    if (!stock)
        return nullptr;

    u8* a2 = drawBlock;

    // 2. Draw-block header: a2[2]=stock+68, a2[3]=stock+76, a2[4]=stock, a2+380=0;
    //    AllocPolysAndPoints(a2, lodArg). (0x5d1144..0x5d115e)
    const u32 vertexCount = rd32(stock, stock::kVertCount);
    const u32 polyCount   = rd32(stock, stock::kPolyCount);
    wr32(a2, hdr::kVertCount, vertexCount);
    wr8(a2, hdr::kFlag380, 0);
    wr32(a2, hdr::kPolyCount, polyCount);
    wrptr(a2, hdr::kStockPtr, reinterpret_cast<uintptr_t>(stock));
    if (h.allocPolysAndPoints)
        h.allocPolysAndPoints(a2, lodArg);

    u8* drawVerts = reinterpret_cast<u8*>(rdptr(a2, hdr::kVertArrPtr));   // a2[0]
    u8* drawPolys = reinterpret_cast<u8*>(rdptr(a2, hdr::kPolyArrPtr));   // a2[1]

    // 3. Per-vertex loop: i < a2[2] + 8 (0x5d116f..0x5d11b5).
    //    draw vert +72 = stockVertArray + i*24; +76 = 0; +64 = +68 = -1.
    // Stock array bases are real heap pointers (engine 32-bit dwords; native ptrs
    // here), so read them with rdptr, not rd32.
    const uintptr_t stockVertArray = rdptr(stock, stock::kVertArray);
    {
        const int n = static_cast<int>(rd32(a2, hdr::kVertCount));
        if (n > 0 && drawVerts) {
            // v10 advances by 24 per iter; the source value is stockVertArray + v10.
            uintptr_t src = stockVertArray;
            for (int i = 0; i < n + 8; ++i) {
                u8* dv = drawVerts + i * layout::kVertStride;
                // +72 source ptr = stockVertArray + i*24 (stored as the source value).
                wrptr(dv, layout::kVertSrcPtr, src);
                wr8(dv, layout::kVertFlag76, 0);
                wr32(dv, layout::kVertCol0, 0xFFFFFFFFu);
                wr32(dv, layout::kVertCol1, 0xFFFFFFFFu);
                src += stock::kStockVertStride;
            }
        }
    }

    // 4. Per-polygon loop (0x5d11bb..0x5d131f).
    const uintptr_t stockPolyArray = rdptr(stock, stock::kPolyArray);
    {
        const int np = static_cast<int>(rd32(a2, hdr::kPolyCount));
        if (np > 0 && drawPolys && drawVerts) {
            for (int p = 0; p < np; ++p) {
                u8* dp = drawPolys + p * layout::kPolyStride;
                u8* sp = reinterpret_cast<u8*>(
                    stockPolyArray +
                    static_cast<uintptr_t>(p) * stock::kStockPolyStride);

                // v13[8]=0; v13[7]=v13[8]; (float)v13[6]=v13[8]; +38 &= 0xFE.
                wr32(dp, layout::kPolyUV2, 0);
                wr32(dp, layout::kPolyUV1, 0);
                wr32(dp, layout::kPolyUV0, 0);
                wr8(dp, layout::kPolyFlag38, rd8(dp, layout::kPolyFlag38) & 0xFE);

                // v13[4] = stockPoly ptr (engine +16).
                wrptr(dp, layout::kPolyStock, reinterpret_cast<uintptr_t>(sp));

                // The 3 vertex ptrs: drawVertBase + 80*idx; mark vert +76 |= 0x80.
                static const int kVertSlots[3] = {
                    layout::kPolyV0, layout::kPolyV1, layout::kPolyV2};
                for (int j = 0; j < 3; ++j) {
                    const u32 idx = rd32(sp, spoly::kIdx0 + 4 * j);
                    u8* vp = drawVerts + static_cast<int>(idx) * layout::kVertStride;
                    wrptr(dp, kVertSlots[j], reinterpret_cast<uintptr_t>(vp));
                    wr8(vp, layout::kVertFlag76,
                        static_cast<u8>(rd8(vp, layout::kVertFlag76) | 0x80u));
                }

                // +36 byte cleared; matIndex = stockPoly+36 (the texture-record index).
                wr8(dp, layout::kPolyFlag36, 0);
                const i32 texRecIdx = static_cast<i32>(rd32(sp, spoly::kTexIdx));
                if (texRecIdx < 0) {
                    // v13[5] = 0 (no texture). (0x5d158e)
                    wrptr(dp, layout::kPolyTex, 0);
                    continue;
                }

                // texRec = dword_1406A84 + (texRecIdx << 7); v13[5] = texRec handle.
                AttachTextureRecord tr =
                    h.textureRecord ? h.textureRecord(texRecIdx)
                                    : AttachTextureRecord{};
                // Default accessor synthesises a non-null handle so the texture-set
                // build still has something to copy when no hook is installed.
                if (!h.textureRecord)
                    tr.handle = static_cast<uintptr_t>(texRecIdx) + 1u;
                wrptr(dp, layout::kPolyTex, tr.handle);

                // Colour/blend dword construction (0x5d125c..0x5d12b1):
                //   enter when texRec+108 != 0xFF || (texRec+110 & 1) || (texRec+110 & 2).
                const bool hasBlend =
                    (tr.trans108 != 0xFF) || (tr.mode110 & 1) || (tr.mode110 & 2);
                if (hasBlend) {
                    u32 v76;
                    if (tr.mode110 & 1) {
                        // mode 1 (alpha): all four bytes = texRec+108 (grey alpha).
                        const u8 a = tr.trans108;
                        v76 = (static_cast<u32>(a) << 24) |
                              (static_cast<u32>(a) << 16) |
                              (static_cast<u32>(a) << 8) |
                              (static_cast<u32>(a));
                    } else {
                        // else: LOWORD = 0xFFFF, BYTE2 = 0xFF, HIBYTE = texRec+108.
                        v76 = (static_cast<u32>(tr.trans108) << 24) |
                              (0xFFu << 16) | 0xFFFFu;
                    }
                    // Every vertex: +64 = +68 = v76.
                    for (int j = 0; j < 3; ++j) {
                        u8* vp = reinterpret_cast<u8*>(rdptr(dp, kVertSlots[j]));
                        wr32(vp, layout::kVertCol0, v76);
                        wr32(vp, layout::kVertCol1, v76);
                    }
                }

                // poly +38 bit3 = (texRec+104 & 1) << 3 (2-sided); each vert +77 =
                // texRec+104 & 1. (0x5d12bb..0x5d12e4)
                const u8 twoSided = tr.flags104 & 1;
                u8 f38 = rd8(dp, layout::kPolyFlag38) & 0xF7;
                f38 = static_cast<u8>(f38 | (twoSided << 3));
                wr8(dp, layout::kPolyFlag38, f38);
                for (int j = 0; j < 3; ++j) {
                    u8* vp = reinterpret_cast<u8*>(rdptr(dp, kVertSlots[j]));
                    wr8(vp, layout::kVertSide77, twoSided);
                }
            }
        }
    }

    // 5. Texture-set build (0x5d1325..0x5d147d).
    wr8(a2, hdr::kFlag381, 0);
    const int matCount = static_cast<int>(rd32(stock, stock::kMatCount));
    // a2[5] = alloc(4 * matCount). We allocate a native uintptr_t per material.
    static thread_local std::vector<uintptr_t> texSetStorage;
    texSetStorage.assign(matCount > 0 ? static_cast<std::size_t>(matCount) : 0, 0);
    u8* texSet = matCount > 0 ? reinterpret_cast<u8*>(texSetStorage.data()) : nullptr;
    wrptr(a2, hdr::kTexSetPtr, reinterpret_cast<uintptr_t>(texSet));

    const uintptr_t stockTexNameArr = rdptr(stock, stock::kTexNameArr);
    if (matCount > 0) {
        const int np = static_cast<int>(rd32(a2, hdr::kPolyCount));
        for (int mat = 0; mat < matCount; ++mat) {
            // Scan draw polys for one whose stockPoly+40 == mat (and has a stock poly).
            if (np > 0 && drawPolys) {
                bool found = false;
                for (int p = 0; p < np; ++p) {
                    u8* dp = drawPolys + p * layout::kPolyStride;
                    u8* sp = reinterpret_cast<u8*>(rdptr(dp, layout::kPolyStock));
                    if (!sp)
                        continue;
                    if (static_cast<int>(rd32(sp, spoly::kMatIdx)) != mat)
                        continue;
                    found = true;
                    const uintptr_t handle = rdptr(dp, layout::kPolyTex);
                    if (handle) {
                        // a2[5][mat] = handle; IncrementRefCount(handle, 1).
                        std::memcpy(texSet + mat * sizeof(uintptr_t), &handle,
                                    sizeof(handle));
                        if (h.textureIncrementRefCount)
                            h.textureIncrementRefCount(handle, 1);
                    } else {
                        // "stock object %s, texture %s not found"
                        if (h.logError) {
                            const char* texName =
                                stockTexNameArr
                                    ? reinterpret_cast<const char*>(
                                          stockTexNameArr +
                                          static_cast<uintptr_t>(mat) * 64u)
                                    : nullptr;
                            h.logError("stock object %s, texture %s not found", name,
                                       texName);
                        }
                    }
                    break;
                }
                (void)found;
            }
        }

        // "Not all Textures used/found!" verification: if any a2[5][k] is 0, log.
        bool allFound = true;
        for (int k = 0; k < matCount; ++k) {
            uintptr_t v;
            std::memcpy(&v, texSet + k * sizeof(uintptr_t), sizeof(v));
            if (v == 0) { allFound = false; break; }
        }
        if (!allFound && h.logError)
            h.logError("Error attaching Stock-Object \"%s\": Not all Textures used/found!",
                       name, nullptr);
    }

    // 6. LOD bookkeeping — only when a2 != node+492 + 1396 (not the "_s" slot).
    //    (0x5d147d..0x5d1534)
    if (node) {
        u8* drawData = reinterpret_cast<u8*>(rdptr(node, kNodeDrawData));
        const bool isSVariant =
            drawData && (a2 == drawData + kSVariantOffset);
        if (!isSVariant && drawData) {
            // ++drawData+2316 (LOD count); node+532 = 4.
            wr8(drawData, kDrawDataLodCount,
                static_cast<u8>(rd8(drawData, kDrawDataLodCount) + 1));
            wr8(node, kNodeByte532, 4);

            uintptr_t activeFrame = rdptr(node, kNodeActiveFrame);  // node+460
            if (!activeFrame) {
                activeFrame = h.selectLodFrame ? h.selectLodFrame(node) : 0;
                wrptr(node, kNodeActiveFrame, activeFrame);
            }
            if (!rdptr(node, kNodeActiveFrame)) {
                // Scan the 3 LOD records (drawData + k*384 + 244) for the first valid.
                // The original tests record+12 (a2[3] polyCount) > 0 && record+8 (a2[2]
                // vertCount) > 0 && record+16 (a2[4] stock ptr) != 0; under the 64-bit
                // draw-block relocation those three live at hdr::kPolyCount / kVertCount /
                // kStockPtr (the offsets AttachStockTextures actually WROTE above).
                for (int off = 0; off < 1152; off += 384) {
                    u8* rec = drawData + off + kLodRecordBase;  // drawData + off + 244
                    const i32 polyC = static_cast<i32>(rd32(rec, hdr::kPolyCount));  // a2[3]
                    const i32 vertC = static_cast<i32>(rd32(rec, hdr::kVertCount));  // a2[2]
                    const uintptr_t stockP = rdptr(rec, hdr::kStockPtr);            // a2[4]
                    if (polyC > 0 && vertC > 0 && stockP != 0) {
                        wrptr(node, kNodeActiveFrame,
                              reinterpret_cast<uintptr_t>(rec));
                        break;
                    }
                }
            }
        }
    }

    // 7. ++stock+476 (refcount); return stock. (0x5d1536..0x5d153d)
    wr32(stock, stock::kRefCount, rd32(stock, stock::kRefCount) + 1);
    return stock;
}

// ---------------------------------------------------------------------------
// Wiring: make AttachStockTextures the real implementation of the
// `attachStockTextures` hook in mesh_lod_name (the leaf VIBE_Mesh_AttachStockObjectLods
// @0x5d1824 invokes). The MeshLodHooks signature is
//   bool(void* object, int drawBlockOffset, int lodArg, const char* stockName)
// where `object` is the node (a1) and `drawBlockOffset` is the byte offset from the
// node's drawData block (244 base / 1396 "_s" / 244+384*lod frame). The original's
// a2 is exactly drawData + drawBlockOffset, so the adapter recovers drawData from
// node+492 and forwards to AttachStockTextures. Returns whether a stock object was
// attached (the original's al = (v75 != 0)).
// ---------------------------------------------------------------------------
static bool AttachStockTexturesHookAdapter(void* object, int drawBlockOffset,
                                           int lodArg, const char* stockName) {
    u8* node = reinterpret_cast<u8*>(object);
    if (!node)
        return false;
    u8* drawData = reinterpret_cast<u8*>(rdptr(node, kNodeDrawData));  // node+492
    if (!drawData)
        return false;
    u8* drawBlock = drawData + drawBlockOffset;
    return AttachStockTextures(node, drawBlock, lodArg, stockName) != nullptr;
}

void InstallStockTextureAttach() {
    MeshLodHooksMut().attachStockTextures = &AttachStockTexturesHookAdapter;
    // Wire the real VIBE_Mesh_SelectLodFrame (0x5adb6c) so step 6 picks the active LOD
    // frame instead of falling straight to the scan fallback (rule 13). Until the live
    // universe runtime calls SetLodSelectView, the no-camera view forces the LOD branch.
    MeshAttachHooksMut().selectLodFrame = &SelectLodFrameForNode;
}

} // namespace guild::render
