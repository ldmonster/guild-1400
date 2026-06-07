#include "test.h"

// Integration: drive render_leaves2's VIBE_Mesh_SetGlobalColorTemp against the
// REAL reconstructed sibling VIBE_Mesh_SetVertexColors (mesh_transform.cpp
// 0x428928) — no mock vertex-colour writer.
//
// SetGlobalColorTemp packs a {b,r,g} payload and walks the object's scene-graph
// node tree, invoking the per-node colour callback (SetVertexColorRgb) on each
// node. The graph walk itself is the one unreconstructed leaf, routed through the
// RenderLeaves2Hooks.walkAndInvoke hook. Here we install a walk hook that does
// the REAL thing the live VIBE_SceneGraph_WalkAndInvoke does: invoke the supplied
// callback on each real node. The callback is render_leaves2's own
// SetVertexColorRgb, which forwards into the genuine SetVertexColors — so the
// colour bytes land in real vertex records via the real sibling, end to end.
#include "render/render_leaves2.h"
#include "render/mesh_transform.h"   // real SetVertexColors (0x428928)

#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

// A real mesh node as SetVertexColors / StampVertexColors read it:
//   obj+460 -> mesh ptr
//   obj+528 / obj+530 -> flag bytes
//   mesh+4  -> vertex-ptr array base (10-ptr stride per submesh; reads 3 per sub)
//   mesh+12 -> submesh count (int)
//   each vertex's +68/+69/+70 receive {g, r, b}.
struct Vertex { std::uint8_t bytes[80]; };
struct Mesh {
    void* vtxArray[10];   // +0..; mesh+4 is &vtxArray[1]? — we model mesh+4 directly
    int   pad8;           // mesh+8
    int   subCount;       // mesh+12
};

// We lay out the object as a raw byte block so the +460/+528/+530 offsets are
// exact; the mesh pointer is stored at +460.
struct Obj {
    std::uint8_t bytes[600];
    Obj() { std::memset(bytes, 0, sizeof bytes); }
    void SetMesh(void* m) { std::memcpy(bytes + 460, &m, sizeof(void*)); }
    std::uint8_t& At(int off) { return bytes[off]; }
};

// mesh layout by raw offset: +4 = ptr-array base, +12 = submesh count.
struct MeshBuf {
    std::uint8_t bytes[64];
    MeshBuf() { std::memset(bytes, 0, sizeof bytes); }
    void SetPtrArray(void* base) { std::memcpy(bytes + 4, &base, sizeof(void*)); }
    void SetSubCount(int n) { std::memcpy(bytes + 12, &n, sizeof(int)); }
};

// The genuine WalkAndInvoke: invoke the per-node callback on the single root node
// (the smallest faithful tree). The callback identity is SetVertexColorRgb; we
// call it with the 3-byte payload exactly as the scene-graph walk would, and
// return 1 (walk completed).
i8 RealWalk(void* /*table*/, void* root, void* callback, int /*mask*/,
            const u8* payload) {
    using NodeCb = i8 (*)(void*, const u8*);
    NodeCb cb = reinterpret_cast<NodeCb>(callback);
    if (cb && root) cb(root, payload);   // -> SetVertexColorRgb -> real SetVertexColors
    return 1;
}

} // namespace

// SetGlobalColorTemp(obj, b, g, r) packs payload {b, r, g} and walks the tree
// invoking SetVertexColorRgb(node, payload). SetVertexColorRgb calls the real
// SetVertexColors(obj, b=payload[0], g=payload[2], r=payload[1]) which stamps
// every vertex's +68=g, +69=r, +70=b and sets obj flags. Verify the colour bytes
// reached real vertex records through the real sibling.
TEST(RenderLeaves2Itest, GlobalColorTempStampsVerticesViaRealSetVertexColors) {
    // Build a one-submesh mesh with 3 real vertices (StampVertexColors reads 3).
    Vertex v0{}, v1{}, v2{};
    std::memset(&v0, 0, sizeof v0);
    std::memset(&v1, 0, sizeof v1);
    std::memset(&v2, 0, sizeof v2);
    void* ptrArray[10] = { &v0, &v1, &v2, nullptr, nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr };

    MeshBuf mesh;
    mesh.SetPtrArray(ptrArray);
    mesh.SetSubCount(1);

    Obj obj;
    obj.SetMesh(mesh.bytes);
    obj.At(528) = 0x01;   // bit0 set => SetGlobalColorTemp skips the +496 dance

    RenderLeaves2Hooks hooks{};
    hooks.walkAndInvoke = RealWalk;   // <-- real callback invocation
    InstallRenderLeaves2Hooks(hooks);

    // Colours: b=0x11, g=0x22, r=0x33. payload = {b, r, g} = {0x11, 0x33, 0x22}.
    // SetVertexColorRgb -> SetVertexColors(obj, b=payload[0]=0x11,
    //   g=payload[2]=0x22, r=payload[1]=0x33). StampVertexColors writes
    //   vtx+68 = g = 0x22, vtx+69 = r = 0x33, vtx+70 = b = 0x11.
    SetGlobalColorTemp(obj.bytes, /*b=*/0x11, /*g=*/0x22, /*r=*/0x33);

    Vertex* verts[3] = { &v0, &v1, &v2 };
    for (Vertex* v : verts) {
        if (v) {
            CHECK_EQ(static_cast<int>(v->bytes[68]), 0x22);   // green
            CHECK_EQ(static_cast<int>(v->bytes[69]), 0x33);   // red
            CHECK_EQ(static_cast<int>(v->bytes[70]), 0x11);   // blue
        }
    }
    // The real SetVertexColors set obj+530 bit1 and obj+528 bit2 (colour active).
    CHECK_EQ(static_cast<int>(obj.At(530) & 0x02), 0x02);
    CHECK_EQ(static_cast<int>(obj.At(528) & 0x04), 0x04);

    InstallRenderLeaves2Hooks(RenderLeaves2Hooks{});   // restore inert defaults
}

// Calling SetVertexColorRgb directly (the same callback the walk dispatches)
// against a real mesh node must produce identical vertex writes — pinning that
// the walk path and a direct call agree on the real sibling's behavior.
TEST(RenderLeaves2Itest, SetVertexColorRgbDirectMatchesRealSibling) {
    Vertex v0{}, v1{}, v2{};
    void* ptrArray[10] = { &v0, &v1, &v2, nullptr, nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr };
    MeshBuf mesh;
    mesh.SetPtrArray(ptrArray);
    mesh.SetSubCount(1);
    Obj obj;
    obj.SetMesh(mesh.bytes);

    const u8 payload[3] = { 0x7E, 0x40, 0x10 };   // {b, r, g}
    i8 rc = SetVertexColorRgb(obj.bytes, payload); // -> real SetVertexColors
    CHECK_EQ(static_cast<int>(rc), 1);

    // SetVertexColors(obj, b=payload[0]=0x7E, g=payload[2]=0x10, r=payload[1]=0x40)
    //   vtx+68 = g = 0x10, vtx+69 = r = 0x40, vtx+70 = b = 0x7E.
    CHECK_EQ(static_cast<int>(v0.bytes[68]), 0x10);
    CHECK_EQ(static_cast<int>(v0.bytes[69]), 0x40);
    CHECK_EQ(static_cast<int>(v0.bytes[70]), 0x7E);
}
