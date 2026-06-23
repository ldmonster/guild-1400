#include "render/mesh_stock_object.h"

#include "render/mesh_asset.h"          // Mesh_LoadByName (0x5d2348 VFS form, REUSED)
#include "render/mesh_lod_name.h"       // BuildTexturePath (0x5d1034, REUSED)
#include "render/mesh_attach_textures.h" // MeshAttachHooksMut().findStockObject (consumer)
#include "util/string_ops.h"            // StrCmpNoCaseN (0x5e0db0, REUSED)

#include <cstring>

// =============================================================================
// guild::render — VIBE_Mesh_LoadAndRegister (0x5d32d4) + VIBE_Mesh_FindStockObject
// (0x5d10d0) + the StockRegistry list. See mesh_stock_object.h for the record
// layout and the two-head list discipline recovered from the disasm.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Raw little-endian field accessors on the stock record (same convention as the
// consumer in mesh_attach_textures.cpp: u32 stores for counts, native uintptr_t for
// pointer-bearing slots).
// ---------------------------------------------------------------------------
namespace {
inline void wr32(u8* p, int off, u32 v) { std::memcpy(p + off, &v, 4); }
inline u32  rd32(const u8* p, int off) { u32 v; std::memcpy(&v, p + off, 4); return v; }
inline void wrptr(u8* p, int off, uintptr_t v) { std::memcpy(p + off, &v, sizeof(v)); }
inline uintptr_t rdptr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}
}  // namespace

// ---------------------------------------------------------------------------
// Hooks (inert defaults). loadMesh defaults to the real Mesh_LoadByName core; the
// others are inert (no .tex set, path-probe permissive) so the load reaches the mesh
// parser and the parser's own success/failure is the gate in a headless build.
// ---------------------------------------------------------------------------
StockObjectHooks& StockObjectHooksMut() {
    static StockObjectHooks g = [] {
        StockObjectHooks h;
        h.loadMesh = &Mesh_LoadByName;  // 0x5d2348 (REUSE)
        return h;
    }();
    return g;
}
const StockObjectHooks& StockHooks() { return StockObjectHooksMut(); }

// ---------------------------------------------------------------------------
// StockRegistry.
// ---------------------------------------------------------------------------
StockRegistry::StockRegistry()
    : anchor_(new u8[stockrec::kSize]()),
      sentinel_(new u8[stockrec::kSize]()) {
    // Init mirror (0x5af984): the search head (anchor's +508 NEXT) starts at the
    // sentinel (empty list); the tail pointer (dword_13FCAEC) starts at the anchor.
    wrptr(anchor_.get(), stockrec::kNextPtr,
          reinterpret_cast<uintptr_t>(sentinel_.get()));
    tail_ = anchor_.get();  // dword_13FCAEC = anchor when empty
}

u8* StockRegistry::AnchorRaw() const { return anchor_.get(); }
u8* StockRegistry::Sentinel() const { return sentinel_.get(); }

u8* StockRegistry::SearchHead() const {
    // dword_13FCCFC == anchor's +508 NEXT field.
    return reinterpret_cast<u8*>(rdptr(anchor_.get(), stockrec::kNextPtr));
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d32d4 — VIBE_Mesh_LoadAndRegister.
//
// Original (1:1):
//   path = BuildTexturePath(name, ".bgf");  if (!path) return 0;
//   copy path -> v13;
//   LoadTextureSet(dir, &texSet, &arg);  if (!texSet) LoadTextureSet(name, &texSet, &arg);
//   stock = LoadBgfFile(path, dir, arg, texSet, dirCtx);   // == Mesh_LoadByName
//   if (stock) {
//     if (texSet && texSet != stock[129]) { FreeDebug(texSet); }   // free the dup tex set
//     stock[123..126] = method ptrs; stock[122] = DeleteStockObject;
//   } else if (texSet) { FreeDebug(texSet); }
//   if (!stock) return 0;
//   // APPEND at the tail:
//   stock[128] = dword_13FCAEC;      prevTail = dword_13FCAEC;  dword_13FCAEC = stock;
//   stock[127] = &sentinel;          *(prevTail + 508) = stock;
//   return stock;
// ---------------------------------------------------------------------------
u8* StockRegistry::LoadAndRegister(const char* name, const char* dir) {
    const StockObjectHooks& h = StockHooks();

    // path = BuildTexturePath(name, ".bgf"); !path -> abort (no .bgf resolved).
    if (h.pathExists && !h.pathExists(name ? name : ""))
        return nullptr;

    // The composed load path the original copies into v13 ("*"+name+".bgf"). The
    // real VFS form (Mesh_LoadByName) opens this leaf name; BuildTexturePath builds
    // the "*"-prefixed key, which the VFS resolver consumes.
    std::string loadPath;
    BuildTexturePath(name, ".bgf", &loadPath);

    // LoadTextureSet(dir) else LoadTextureSet(name) — engine leaf (inert default).
    void* texSet = nullptr;
    int   texArg = 0;
    if (h.loadTextureSet) {
        h.loadTextureSet(dir ? dir : "", &texSet, &texArg);
        if (!texSet)
            h.loadTextureSet(name ? name : "", &texSet, &texArg);
    }

    // stock = LoadBgfFile(...)  == Mesh_LoadByName (REUSE). The mesh name stored in
    // the record is the name key (the upper-cased path in the original).
    auto node = std::make_unique<StockObject>();
    const std::string meshName = name ? name : "";
    const char* openName = loadPath.c_str();
    bool ok = h.loadMesh ? h.loadMesh(openName, meshName, node->mesh) : false;

    if (!ok) {
        // else if (texSet) FreeDebug(texSet);  (the texSet is owned by the leaf hook;
        // the inert default never allocates one, so there is nothing to free here.)
        return nullptr;  // !stock -> return 0, register nothing.
    }

    u8* stock = node->Raw();

    // --- populate the raw record at the consumer offsets ---
    // name (+0, 63 chars + NUL): the registry key (the engine kept the mesh name).
    {
        std::memset(stock + stockrec::kName, 0, stockrec::kNameLen + 1);
        const char* src = node->mesh.name.empty() ? meshName.c_str()
                                                   : node->mesh.name.c_str();
        std::strncpy(reinterpret_cast<char*>(stock + stockrec::kName), src,
                     stockrec::kNameLen);
        stock[stockrec::kName + stockrec::kNameLen] = '\0';
    }

    // counts (+68 / +76 / +480) and refcount (+476, starts at 1 — AttachStockTextures
    // ++ it; DeleteStockObject -- it).
    wr32(stock, stockrec::kVertCount,
         static_cast<u32>(node->mesh.vertexCount));
    wr32(stock, stockrec::kPolyCount,
         static_cast<u32>(node->mesh.polyCount));
    wr32(stock, stockrec::kMatCount,
         static_cast<u32>(node->mesh.materialCount));
    wr32(stock, stockrec::kRefCount, 1);

    // array pointer slots (+540 / +548): the OWNED parsed Mesh arrays (24-byte verts,
    // 56-byte polys — exactly the consumer's strides; no copy, no re-parse).
    wrptr(stock, stockrec::kVertArray,
          reinterpret_cast<uintptr_t>(node->mesh.vertices.data()));
    wrptr(stock, stockrec::kPolyArray,
          reinterpret_cast<uintptr_t>(node->mesh.polygons.data()));

    // texture-name array (+556): the packed 64-byte name block (engine +516 / stock[129]).
    node->texNames = node->mesh.materialNames;  // copy so the slot outlives the parse
    wrptr(stock, stockrec::kTexNameArr,
          node->texNames.empty()
              ? 0
              : reinterpret_cast<uintptr_t>(node->texNames.data()));

    // method-ptr table (stock[122..126]).
    wr32(stock, stockrec::kMethodDelete, kMethodDeleteStockObject);
    wr32(stock, stockrec::kMethod123,    kMethodTransformVertexNormals);
    wr32(stock, stockrec::kMethod124,    kMethodInterpolateMorphVertices);
    wr32(stock, stockrec::kMethod125,    kMethodComputeBoundingBox);
    wr32(stock, stockrec::kMethod126,    kMethodGetBoundingRadius);

    // --- APPEND at the tail (the original's link block) ---
    u8* prevTail = tail_;                                  // v12 = dword_13FCAEC
    wrptr(stock, stockrec::kPrevPtr, reinterpret_cast<uintptr_t>(prevTail));  // [128]=prev
    tail_ = stock;                                          // dword_13FCAEC = stock
    wrptr(stock, stockrec::kNextPtr,
          reinterpret_cast<uintptr_t>(sentinel_.get()));   // [127] = &sentinel
    wrptr(prevTail, stockrec::kNextPtr,
          reinterpret_cast<uintptr_t>(stock));             // prevTail+508 = stock

    nodes_.push_back(std::move(node));
    return stock;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5d10d0 — VIBE_Mesh_FindStockObject.
//   v2 = dword_13FCCFC;  if (v2 == sentinel) return 0;
//   while (StrCmpNoCaseN(name, v2, 63)) { v2 = *(v2+508); if (v2 == sentinel) return 0; }
//   return v2;
// (StrCmpNoCaseN returns 0 == equal -> the while exits -> v2 is the match.)
// ---------------------------------------------------------------------------
u8* StockRegistry::FindStockObject(const char* name) {
    u8* sentinel = sentinel_.get();
    u8* v2 = SearchHead();             // dword_13FCCFC (anchor's +508)
    if (v2 == sentinel)
        return nullptr;
    while (guild::util::StrCmpNoCaseN(
               name, reinterpret_cast<const char*>(v2), stockrec::kNameLen) != 0) {
        v2 = reinterpret_cast<u8*>(rdptr(v2, stockrec::kNextPtr));  // *(v2+508)
        if (v2 == sentinel)
            return nullptr;
    }
    return v2;
}

// ---------------------------------------------------------------------------
// Process-global registry + wiring.
// ---------------------------------------------------------------------------
StockRegistry& Registry() {
    static StockRegistry g;
    return g;
}

namespace {
// Adapter: the consumer's findStockObject hook signature is u8*(const char*).
u8* RegistryFindAdapter(const char* name) {
    return Registry().FindStockObject(name);
}
}  // namespace

void InstallStockObjectRegistry() {
    MeshAttachHooksMut().findStockObject = &RegistryFindAdapter;
}

}  // namespace guild::render
