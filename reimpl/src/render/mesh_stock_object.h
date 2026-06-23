#pragma once
#include "guild/common/types.h"
#include "render/mesh_load.h"

#include <memory>
#include <string>
#include <vector>

// =============================================================================
// guild::render — the STOCK-OBJECT registry: the raw stock-object record + the
// load-and-register / find-by-name pair, reconstructed 1:1 from gilde.exe.
//
//   0x5D32D4  VIBE_Mesh_LoadAndRegister   — build "*"+name+".bgf", load the .tex
//                                            set, run VIBE_Mesh_LoadBgfFile, set the
//                                            method-ptr table (stock[122..126]) and
//                                            APPEND the new stock object onto the
//                                            registry's doubly-linked list.
//   0x5D10D0  VIBE_Mesh_FindStockObject   — walk the registry forward from the head
//                                            anchor's next field (dword_13FCCFC),
//                                            sentinel == &unk_13FC8EC, next ptr at
//                                            obj+508; case-insensitive 63-char name
//                                            compare (VIBE_Util_StrCmpNoCaseN). Return
//                                            the match, or 0.
//
// THE LIST DISCIPLINE (recovered from 0x5af984 init + 0x5d32d4 insert +
// 0x5d1968 DeleteStockObject unlink + 0x5affec DisposeAllObjects walk):
//
//   The registry is a DOUBLY-LINKED list with a fixed HEAD-ANCHOR node and a fixed
//   TAIL-SENTINEL node, both in the engine's .bss:
//       head-anchor  = unk_13FCB00      (a dummy node; never a real stock object)
//       tail-sentinel = unk_13FC8EC
//   Per-node link fields:
//       obj+508 (stock[127])  = NEXT  ptr (forward; what FindStockObject walks)
//       obj+512 (stock[128])  = PREV  ptr (backward; used by the unlink)
//
//   Two head GLOBALS, which are NOT two lists — they are two views of the SAME list:
//       dword_13FCCFC  (the SEARCH head FindStockObject reads) is LITERALLY the
//                      head-anchor's +508 (NEXT) field:  0x13FCB00 + 0x1FC == 0x13FCCFC.
//                      So "the search head" == "the anchor's next" == the FIRST real
//                      node (or the sentinel when the list is empty).
//       dword_13FCAEC  (the INSERT head LoadAndRegister reads/writes) is the TAIL
//                      pointer: it points at the LAST real node, or at the head-anchor
//                      unk_13FCB00 when the list is empty.
//   Init (0x5af984): dword_13FCCFC = sentinel (empty), dword_13FCAEC = anchor.
//   Insert (APPEND at the tail, 0x5d342d):
//       new->prev(+512) = dword_13FCAEC            // old tail
//       dword_13FCAEC   = new                       // tail = new
//       new->next(+508) = sentinel                  // new is the new tail
//       *(old_tail + 508) = new                     // old tail's NEXT -> new
//                                                    //   (when empty, old_tail ==
//                                                    //    anchor, so this write IS
//                                                    //    dword_13FCCFC = new)
//   Unlink (0x5d1968, refcount<=0): next->prev = this->prev; prev->next = this->next.
//
//   So FindStockObject (forward from the anchor's next) and DisposeAllObjects (forward
//   from dword_13FCCFC) traverse the exact chain LoadAndRegister appends to. This
//   reconstruction models the anchor + sentinel + tail pointer explicitly in a
//   StockRegistry so the single live list is faithful and re-entrant.
//
// THE RECORD. The original stock object is a fixed-offset 0x20C+-byte block; the
// reconstruction (mesh_attach_textures.* — the consumer) reads a documented subset
// of those offsets, with the pointer-bearing fields RELOCATED to a reserved >= 540
// region for LP64 safety while the non-pointer count/refcount fields keep their exact
// engine byte offsets. StockObject below is that raw block (`u8 bytes[kSize]`) plus
// the owned parsed Mesh whose vertex/polygon arrays the pointer slots reference. The
// offsets here MATCH the consumer's `namespace stock` contract byte-for-byte:
//       +0    name (63 chars + NUL)          (engine offset; FindStockObject reads it)
//       +68   vertex count   (u32)           (engine offset; consumer stock::kVertCount)
//       +76   polygon count  (u32)           (engine offset; consumer stock::kPolyCount)
//       +476  refcount       (u32)           (engine offset; consumer stock::kRefCount)
//       +480  material count (u32)           (engine offset; consumer stock::kMatCount)
//       +488  method-ptr stock[122] DeleteStockObject  (u32 identifier tag)
//       +492  method-ptr stock[123] TransformVertexNormals
//       +496  method-ptr stock[124] InterpolateMorphVertices
//       +500  method-ptr stock[125] ComputeBoundingBox
//       +504  method-ptr stock[126] GetBoundingRadius
//       +540  vertex array ptr   (native)    (consumer stock::kVertArray; engine +64)
//       +548  polygon array ptr  (native)    (consumer stock::kPolyArray; engine +72)
//       +556  texture-name array ptr (native)(consumer stock::kTexNameArr; engine +516)
//       +564  NEXT ptr (native)              (engine +508 stock[127]; FindStockObject walks it)
//       +572  PREV ptr (native)              (engine +512 stock[128]; the unlink uses it)
//   (the four pointer fields are relocated to native-width slots >= 540 to avoid the
//    32-bit-vs-64-bit overlap; see stockrec:: below.)
//   The vertex array is 24-byte MeshVertex records (consumer stock::kStockVertStride),
//   the polygon array is 56-byte MeshPolygon records (kStockPolyStride): the parsed
//   Mesh's `vertices`/`polygons` ARE exactly those strides (static_assert'd in
//   mesh_load.h), so the slots point straight at the owned Mesh — no re-parse, no copy.
//
// GENUINE ENGINE LEAVES routed through StockObjectHooks (inert defaults):
//   0x5D2348  VIBE_Mesh_LoadBgfFile   — the .bgf parse + post-process. REUSED via the
//                                       loadMesh hook (default: render::Mesh_LoadByName).
//   0x5D2240  VIBE_Mesh_LoadTextureSet — the per-dir .tex set loader (engine leaf;
//                                       default no-op leaving texSet null -> the
//                                       LoadBgfFile-owns-the-texname leg).
//   Memory_AllocDebug/FreeDebug, and the mesh method-fn bodies (0x5c9c58 / 0x5c953c /
//   0x5c9e64 / 0x5d329c / 0x5d1968) stay as named method-ptr IDENTIFIERS (see the
//   kMethod* tags) — the record stores their identity, not a live function pointer.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// The method-ptr table identifiers (stock[122..126]). The original stored real
// function pointers (VIBE_Mesh_DeleteStockObject @0x5d1968, etc.); this
// reconstruction stores a stable identifier tag per slot so the table is set
// faithfully and is golden-testable, and so the DeleteStockObject slot the
// DisposeAllObjects walk invokes (stock+488) is identifiable.
// ---------------------------------------------------------------------------
enum MeshMethodId : u32 {
    kMethodNone                     = 0,
    kMethodDeleteStockObject        = 0x5d1968,  // stock[122] +488
    kMethodTransformVertexNormals   = 0x5c9c58,  // stock[123] +492
    kMethodInterpolateMorphVertices = 0x5c953c,  // stock[124] +496
    kMethodComputeBoundingBox       = 0x5c9e64,  // stock[125] +500
    kMethodGetBoundingRadius        = 0x5d329c,  // stock[126] +504
};

// ---------------------------------------------------------------------------
// Raw stock-object record byte offsets (the consumer contract; see header above).
// ---------------------------------------------------------------------------
namespace stockrec {
constexpr int kName        = 0;     // 63 chars + NUL
constexpr int kNameLen     = 63;
constexpr int kVertCount   = 68;    // u32
constexpr int kPolyCount   = 76;    // u32
constexpr int kRefCount    = 476;   // u32
constexpr int kMatCount    = 480;   // u32
constexpr int kMethodDelete = 488;  // stock[122] u32 id
constexpr int kMethod123    = 492;  // stock[123] u32 id
constexpr int kMethod124    = 496;  // stock[124] u32 id
constexpr int kMethod125    = 500;  // stock[125] u32 id
constexpr int kMethod126    = 504;  // stock[126] u32 id
// Pointer-bearing slots (native uintptr_t width). In the 32-bit original these were
// adjacent 4-byte dwords at +508/+512/+64/+72/+516; at native 8-byte pointer width
// those would OVERLAP each other (e.g. +508 and +512 collide), so — exactly like the
// consumer (mesh_attach_textures.cpp `namespace stock`) does for the array ptrs —
// every pointer field is RELOCATED to a dedicated 8-byte slot in a reserved region.
// The non-pointer count/refcount/method-id fields keep their exact engine byte
// offsets above. This is a private in-process layout contract (the block is never
// serialised), and matches the consumer's kVertArray/kPolyArray/kTexNameArr exactly.
constexpr int kVertArray    = 540;  // native ptr (engine +64; consumer stock::kVertArray)
constexpr int kPolyArray    = 548;  // native ptr (engine +72; consumer stock::kPolyArray)
constexpr int kTexNameArr   = 556;  // native ptr (engine +516; consumer stock::kTexNameArr)
constexpr int kNextPtr      = 564;  // native ptr (engine +508 stock[127]; forward link)
constexpr int kPrevPtr      = 572;  // native ptr (engine +512 stock[128]; backward link)
constexpr int kSize         = 584;  // block size (>= kPrevPtr + 8, padded)
}  // namespace stockrec

// ---------------------------------------------------------------------------
// StockObject — the raw fixed-offset block PLUS the owned parsed Mesh the pointer
// slots reference. `Raw()` is the `u8*` the consumer (AttachStockTextures) reads at
// the offsets above; FindStockObject returns it too.
// ---------------------------------------------------------------------------
struct StockObject {
    u8   bytes[stockrec::kSize] = {};  // the raw record (zero-initialised)
    Mesh mesh;                          // the owned parsed geometry (vert/poly arrays)
    std::vector<char> texNames;         // the +516 packed 64-byte name block (copied)

    u8*       Raw()       { return bytes; }
    const u8* Raw() const { return bytes; }
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in mesh_stock_object.cpp).
// ---------------------------------------------------------------------------
struct StockObjectHooks {
    // VIBE_Mesh_LoadBgfFile @0x5d2348 (REUSED). Load+parse the .bgf named by `path`
    // (with the mesh name `name`) into `out`. Returns true on success. Default:
    // render::Mesh_LoadByName (the real VFS+parse+post-process core). Tests install a
    // mock that fills `out` from an in-memory Mesh.
    bool (*loadMesh)(const char* path, const std::string& name, Mesh& out) = nullptr;

    // VIBE_Mesh_LoadTextureSet @0x5d2240 (engine leaf). Given a dir/name key, resolve
    // the per-dir .tex set; *outTexSet is the resolved set ptr (0 if none), *outArg an
    // out-param the original threads into LoadBgfFile. Default: no-op (leaves null ->
    // the LoadBgfFile-owns-the-texname leg, matching the original's fallback).
    void (*loadTextureSet)(const char* key, void** outTexSet, int* outArg) = nullptr;

    // VIBE_Mesh_BuildTexturePath existence probe (the original aborts the load when
    // BuildTexturePath returns null — no .bgf resolved). Default: returns true so the
    // load proceeds to loadMesh (whose own failure is the real gate headless). When a
    // real VFS is bound this routes through render::BuildTexturePath.
    bool (*pathExists)(const char* name) = nullptr;
};

StockObjectHooks&       StockObjectHooksMut();
const StockObjectHooks& StockHooks();

// ---------------------------------------------------------------------------
// StockRegistry — the single live stock-object list (anchor + sentinel + tail
// pointer + owned nodes). Models the engine's dword_13FCCFC / dword_13FCAEC /
// unk_13FCB00 / unk_13FC8EC globals as one re-entrant object.
// ---------------------------------------------------------------------------
class StockRegistry {
public:
    StockRegistry();

    // gilde.exe 0x5D32D4 — VIBE_Mesh_LoadAndRegister.
    //   name : the mesh name key (a1) — "*"+name+".bgf" is the load path / the
    //          registry key compared by FindStockObject.
    //   dir  : the texture-set dir key (a2) — passed to loadTextureSet / loadMesh.
    // Loads the mesh (REUSE), populates a StockObject (name/counts/arrays/matCount/
    // texNames), sets the method-ptr table, APPENDS it to the list, and returns the
    // raw record ptr. Returns null on load failure (and registers nothing).
    u8* LoadAndRegister(const char* name, const char* dir);

    // gilde.exe 0x5D10D0 — VIBE_Mesh_FindStockObject. Walk forward from the head
    // anchor's next (the search head); case-insensitive 63-char name match; return the
    // matched raw record ptr, or null (empty list / miss).
    u8* FindStockObject(const char* name);

    std::size_t size() const { return nodes_.size(); }

    // The head-anchor's NEXT field == the engine's dword_13FCCFC (first real node or
    // sentinel). Exposed for tests asserting the list discipline.
    u8* SearchHead() const;
    // The tail pointer == the engine's dword_13FCAEC (last real node or the anchor).
    u8* TailPointer() const { return tail_; }
    // The sentinel address (== &unk_13FC8EC).
    u8* Sentinel() const;

private:
    u8* AnchorRaw() const;

    // The anchor + sentinel are fixed dummy blocks; only their +508/+512 link fields
    // are read/written. We back them with kSize byte buffers so pointer-slot reads are
    // in-bounds (the engine's blocks are full-size dummies too).
    std::unique_ptr<u8[]> anchor_;     // unk_13FCB00 (head anchor)
    std::unique_ptr<u8[]> sentinel_;   // unk_13FC8EC (tail sentinel)
    u8* tail_ = nullptr;               // dword_13FCAEC (== anchor_ when empty)
    std::vector<std::unique_ptr<StockObject>> nodes_;  // owned real nodes
};

// The process-global registry the render path uses (mirrors the engine's single set
// of globals). The wiring installs FindStockObject against THIS instance.
StockRegistry& Registry();

// ---------------------------------------------------------------------------
// Wire the real StockRegistry::FindStockObject as the `findStockObject` hook the
// consumer (VIBE_Mesh_AttachStockTextures @0x5d1114) calls. Idempotent.
// ---------------------------------------------------------------------------
void InstallStockObjectRegistry();

}  // namespace guild::render
