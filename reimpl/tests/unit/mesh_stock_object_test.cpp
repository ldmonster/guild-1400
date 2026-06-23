#include "test.h"

#include "render/mesh_stock_object.h"
#include "render/mesh_attach_textures.h"
#include "render/mesh_load.h"

#include <cstring>
#include <string>

// =============================================================================
// MeshStockObject — golden tests for VIBE_Mesh_LoadAndRegister (0x5d32d4) +
// VIBE_Mesh_FindStockObject (0x5d10d0) + the StockRegistry list. No assets: the
// loadMesh hook is mocked to fill an in-memory parsed Mesh, so the record
// population + the two-head doubly-linked list discipline are exercised directly.
// =============================================================================
using namespace guild;
using namespace guild::render;

namespace {

// Build a tiny in-memory parsed Mesh (engine-stride records).
Mesh MakeMesh(const std::string& name, int verts, int polys, int mats) {
    Mesh m;
    m.name = name;
    m.vertexCount = verts;
    m.polyCount = polys;
    m.materialCount = mats;
    // engine allocates count+8 vertex slots (the 8 AABB corner verts).
    m.vertices.resize(static_cast<std::size_t>(verts) + 8);
    for (int i = 0; i < verts + 8; ++i) {
        m.vertices[i].pos[0] = static_cast<float>(i);
        m.vertices[i].pos[1] = static_cast<float>(i) + 0.5f;
        m.vertices[i].pos[2] = -static_cast<float>(i);
    }
    m.polygons.resize(static_cast<std::size_t>(polys));
    for (int p = 0; p < polys; ++p) {
        m.polygons[p].vtx[0] = static_cast<u32>((p * 3 + 0) % (verts > 0 ? verts : 1));
        m.polygons[p].vtx[1] = static_cast<u32>((p * 3 + 1) % (verts > 0 ? verts : 1));
        m.polygons[p].vtx[2] = static_cast<u32>((p * 3 + 2) % (verts > 0 ? verts : 1));
        m.polygons[p].texId = -1;
        m.polygons[p].matIndex = p % (mats > 0 ? mats : 1);
    }
    // packed 64-byte name block (one per material).
    m.materialNames.assign(static_cast<std::size_t>(mats) * 64, 0);
    for (int k = 0; k < mats; ++k) {
        std::string tn = "TEX" + std::to_string(k);
        std::strncpy(m.materialNames.data() + k * 64, tn.c_str(), 63);
    }
    return m;
}

// A mock loadMesh that serves a fixed table of meshes keyed by the mesh NAME the
// loader stores (the openName carries "*"+name+".bgf"; we key on the `name` arg).
struct MockLoader {
    // name -> mesh template
    static Mesh* g_a;
    static Mesh* g_b;
    static bool  g_fail;

    static bool Load(const char* /*path*/, const std::string& name, Mesh& out) {
        if (g_fail)
            return false;
        if (g_a && name == g_a->name) { out = *g_a; return true; }
        if (g_b && name == g_b->name) { out = *g_b; return true; }
        return false;
    }
};
Mesh* MockLoader::g_a = nullptr;
Mesh* MockLoader::g_b = nullptr;
bool  MockLoader::g_fail = false;

// Install the mock loader and return a fresh registry to test against.
void InstallMock() {
    StockObjectHooksMut().loadMesh = &MockLoader::Load;
    StockObjectHooksMut().loadTextureSet = nullptr;  // inert
    StockObjectHooksMut().pathExists = nullptr;       // permissive
}

// Raw field readers matching the record contract.
u32 Rd32(const u8* p, int off) { u32 v; std::memcpy(&v, p + off, 4); return v; }
uintptr_t RdPtr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}

}  // namespace

// (a) LoadAndRegister builds a StockObject whose counts/arrays/name match the mesh.
TEST(MeshStockObject, LoadAndRegisterPopulatesRecord) {
    InstallMock();
    Mesh a = MakeMesh("BARREL", 12, 8, 3);
    MockLoader::g_a = &a;
    MockLoader::g_b = nullptr;
    MockLoader::g_fail = false;

    StockRegistry reg;
    u8* stock = reg.LoadAndRegister("BARREL", "OBJ");
    CHECK(stock != nullptr);
    if (!stock) return;

    // name at +0 (63 chars), case-preserved.
    CHECK(std::strcmp(reinterpret_cast<const char*>(stock + stockrec::kName),
                      "BARREL") == 0);
    // counts at the engine offsets.
    CHECK_EQ(Rd32(stock, stockrec::kVertCount), 12u);
    CHECK_EQ(Rd32(stock, stockrec::kPolyCount), 8u);
    CHECK_EQ(Rd32(stock, stockrec::kMatCount), 3u);
    // refcount starts at 1.
    CHECK_EQ(Rd32(stock, stockrec::kRefCount), 1u);

    // vertArr / polyArr point at 24-byte / 56-byte records (the consumer strides).
    u8* vertArr = reinterpret_cast<u8*>(RdPtr(stock, stockrec::kVertArray));
    u8* polyArr = reinterpret_cast<u8*>(RdPtr(stock, stockrec::kPolyArray));
    CHECK(vertArr != nullptr);
    CHECK(polyArr != nullptr);
    // first vertex pos round-trips (MeshVertex pos[0] at +0).
    if (vertArr) {
        float x; std::memcpy(&x, vertArr, 4);
        CHECK(x == 0.0f);
    }
    // poly[1] stock vertex index at +24 (spoly::kIdx0) — 56-byte stride.
    if (polyArr) {
        u32 idx; std::memcpy(&idx, polyArr + 56 + 24, 4);
        CHECK_EQ(idx, a.polygons[1].vtx[0]);
        u32 matIdx; std::memcpy(&matIdx, polyArr + 56 + 40, 4);  // +40 matIndex
        CHECK_EQ(matIdx, static_cast<u32>(a.polygons[1].matIndex));
    }
}

// (d) the method-ptr identifiers are set.
TEST(MeshStockObject, MethodPtrTableSet) {
    InstallMock();
    Mesh a = MakeMesh("CART", 6, 4, 1);
    MockLoader::g_a = &a; MockLoader::g_b = nullptr; MockLoader::g_fail = false;

    StockRegistry reg;
    u8* stock = reg.LoadAndRegister("CART", "OBJ");
    CHECK(stock != nullptr);
    if (!stock) return;
    CHECK_EQ(Rd32(stock, stockrec::kMethodDelete), (u32)kMethodDeleteStockObject);
    CHECK_EQ(Rd32(stock, stockrec::kMethod123),    (u32)kMethodTransformVertexNormals);
    CHECK_EQ(Rd32(stock, stockrec::kMethod124),    (u32)kMethodInterpolateMorphVertices);
    CHECK_EQ(Rd32(stock, stockrec::kMethod125),    (u32)kMethodComputeBoundingBox);
    CHECK_EQ(Rd32(stock, stockrec::kMethod126),    (u32)kMethodGetBoundingRadius);
}

// (b) it links into the list and FindStockObject finds it (case-insensitive, 63
//     char); null on a miss and on an empty list.
TEST(MeshStockObject, FindAfterRegisterCaseInsensitive) {
    InstallMock();
    Mesh a = MakeMesh("BARREL", 12, 8, 3);
    MockLoader::g_a = &a; MockLoader::g_b = nullptr; MockLoader::g_fail = false;

    StockRegistry reg;
    // empty list -> miss.
    CHECK(reg.FindStockObject("BARREL") == nullptr);
    CHECK_EQ(reg.size(), (std::size_t)0);
    // search head is the sentinel when empty.
    CHECK(reg.SearchHead() == reg.Sentinel());
    // tail pointer is the anchor when empty (== NOT the sentinel).
    CHECK(reg.TailPointer() != nullptr);
    CHECK(reg.TailPointer() != reg.Sentinel());

    u8* stock = reg.LoadAndRegister("BARREL", "OBJ");
    CHECK(stock != nullptr);
    CHECK_EQ(reg.size(), (std::size_t)1);

    // exact + case-insensitive find.
    CHECK(reg.FindStockObject("BARREL") == stock);
    CHECK(reg.FindStockObject("barrel") == stock);
    CHECK(reg.FindStockObject("BaRrEl") == stock);
    // miss.
    CHECK(reg.FindStockObject("CRATE") == nullptr);

    // list discipline: search head now == the node; node's next == sentinel;
    // node's prev == anchor (the empty-list tail); tail pointer == the node.
    CHECK(reg.SearchHead() == stock);
    CHECK(reinterpret_cast<u8*>(RdPtr(stock, stockrec::kNextPtr)) == reg.Sentinel());
    CHECK(reg.TailPointer() == stock);
}

// 63-char comparison boundary: names equal in the first 63 chars match even if they
// differ at char 63+ (the original compares exactly 63).
TEST(MeshStockObject, Find63CharBoundary) {
    InstallMock();
    std::string base(63, 'A');             // 63 'A's
    std::string stored = base + "X";        // 64 chars; stored truncated to 63 in record
    Mesh a = MakeMesh(stored, 3, 1, 1);
    MockLoader::g_a = &a; MockLoader::g_b = nullptr; MockLoader::g_fail = false;

    StockRegistry reg;
    u8* stock = reg.LoadAndRegister(stored.c_str(), "OBJ");
    CHECK(stock != nullptr);
    if (!stock) return;
    // the record holds only 63 chars.
    CHECK_EQ(std::strlen(reinterpret_cast<const char*>(stock + stockrec::kName)),
             (std::size_t)63);
    // a query that agrees in the first 63 chars but differs after -> still a match.
    std::string query = base + "Y";
    CHECK(reg.FindStockObject(query.c_str()) == stock);
    // a query differing within the first 63 chars -> miss.
    std::string miss = std::string(62, 'A') + "B";
    CHECK(reg.FindStockObject(miss.c_str()) == nullptr);
}

// (c) registering two meshes -> both findable; list discipline correct (append order).
TEST(MeshStockObject, TwoMeshesBothFindable) {
    InstallMock();
    Mesh a = MakeMesh("BARREL", 12, 8, 3);
    Mesh b = MakeMesh("CRATE", 7, 5, 2);
    MockLoader::g_a = &a; MockLoader::g_b = &b; MockLoader::g_fail = false;

    StockRegistry reg;
    u8* sa = reg.LoadAndRegister("BARREL", "OBJ");
    u8* sb = reg.LoadAndRegister("CRATE", "OBJ");
    CHECK(sa != nullptr);
    CHECK(sb != nullptr);
    CHECK(sa != sb);
    CHECK_EQ(reg.size(), (std::size_t)2);

    CHECK(reg.FindStockObject("BARREL") == sa);
    CHECK(reg.FindStockObject("CRATE") == sb);

    // append order: search head is the FIRST registered (BARREL); BARREL.next == CRATE;
    // CRATE.next == sentinel; tail pointer == CRATE (last registered).
    CHECK(reg.SearchHead() == sa);
    CHECK(reinterpret_cast<u8*>(RdPtr(sa, stockrec::kNextPtr)) == sb);
    CHECK(reinterpret_cast<u8*>(RdPtr(sb, stockrec::kNextPtr)) == reg.Sentinel());
    CHECK(reg.TailPointer() == sb);
    // prev links: CRATE.prev == BARREL; BARREL.prev == anchor (not sentinel, not a node).
    CHECK(reinterpret_cast<u8*>(RdPtr(sb, stockrec::kPrevPtr)) == sa);
    CHECK(reinterpret_cast<u8*>(RdPtr(sa, stockrec::kPrevPtr)) != reg.Sentinel());
    CHECK(reinterpret_cast<u8*>(RdPtr(sa, stockrec::kPrevPtr)) != sa);
    CHECK(reinterpret_cast<u8*>(RdPtr(sa, stockrec::kPrevPtr)) != sb);
}

// (e) load failure (loadMesh returns false) -> LoadAndRegister returns null and
//     registers nothing (list unchanged).
TEST(MeshStockObject, LoadFailureRegistersNothing) {
    InstallMock();
    MockLoader::g_a = nullptr; MockLoader::g_b = nullptr; MockLoader::g_fail = true;

    StockRegistry reg;
    u8* stock = reg.LoadAndRegister("BARREL", "OBJ");
    CHECK(stock == nullptr);
    CHECK_EQ(reg.size(), (std::size_t)0);
    CHECK(reg.FindStockObject("BARREL") == nullptr);
    // list still empty: search head == sentinel.
    CHECK(reg.SearchHead() == reg.Sentinel());

    // a name with no matching mesh template (loadMesh returns false for unknown) also
    // registers nothing.
    MockLoader::g_fail = false;
    Mesh a = MakeMesh("BARREL", 4, 2, 1);
    MockLoader::g_a = &a;
    CHECK(reg.LoadAndRegister("UNKNOWN", "OBJ") == nullptr);
    CHECK_EQ(reg.size(), (std::size_t)0);
}

// Wiring smoke: InstallStockObjectRegistry binds the consumer's findStockObject hook
// to the process-global registry, so AttachStockTextures resolves through it.
TEST(MeshStockObject, WiringInstallsFindHook) {
    InstallMock();
    InstallStockObjectRegistry();
    CHECK(AttachHooks().findStockObject != nullptr);

    // register into the process-global registry and confirm the hook resolves it.
    Mesh a = MakeMesh("WHEEL", 5, 3, 1);
    MockLoader::g_a = &a; MockLoader::g_b = nullptr; MockLoader::g_fail = false;
    u8* stock = Registry().LoadAndRegister("WHEEL", "OBJ");
    CHECK(stock != nullptr);
    if (AttachHooks().findStockObject)
        CHECK(AttachHooks().findStockObject("WHEEL") == stock);
    // restore the inert default so other suites are unaffected.
    MeshAttachHooksMut().findStockObject = nullptr;
}
