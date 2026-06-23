#include "test.h"

// =============================================================================
// FULL-CHAIN real-asset e2e for the model-load -> scene-node -> draw-block pipeline
// (rule 13: drive the LIVE wiring, not a mock). After InstallRealObjectAttachWiring()
// a single ObjectAttachToUniverseNode(parent=0, pos, "<real Objects.BIN member>",
// worldRot, lodArg) runs the whole chain on REAL parsed .bgf geometry:
//
//   VIBE_Object_AttachToUniverseNode @0x5b3e30
//     -> Object_Spawn(4,name) @0x5b054c   (REAL: verbatim node + ObjectInitStruct)
//     -> Mesh_FindStockObject / Mesh_LoadOrFindByName @0x5d345c
//          -> StockRegistry::LoadAndRegister @0x5d32d4 (REUSE; REAL parsed mesh)
//     -> Mesh_AttachStockObjectLods @0x5d1824
//          -> Object_AllocDrawData @0x5b107c (REAL; node+492 draw block)
//          -> AttachStockTextures @0x5d1114 (REAL; fills the per-frame draw block)
//               -> FindStockObject @0x5d10d0 (REAL registry)
//               -> AllocPolysAndPoints @0x5b0c10 (REAL alloc)
//     -> per-LOD texture-upload loop (GAP: GPU upload inert headless)
//     -> SetPosition @0x5af38c / SetWorldTranslation @0x5af50c (REAL record writes)
//     -> LinkIntoScene @0x5b0a20 (observer; full splice is the live-universe gap)
//
// Asserts the result is a live, renderable node: a real node spawned, its name set,
// the stock mesh loaded + found in the stock list, the draw block populated with
// vert/poly counts matching the real .bgf, SetPosition/SetWorldTranslation applied,
// and LinkIntoScene invoked for the root attach.
//
// GUARDED: clean skip (zero checks) when the real game dir / Objects.BIN is absent,
// or when no member parses with geometry (it never fakes a mesh — rule 8). Honors
// GUILD_GAME_DIR. Unique suite: ObjectAttachChain.
// =============================================================================
#include "app/real_boot.h"
#include "io/vfs.h"
#include "render/mesh_asset.h"
#include "render/mesh_load.h"
#include "render/mesh_stock_object.h"
#include "render/mesh_lod_name.h"
#include "render/mesh_attach_textures.h"
#include "sim/object_attach_wiring.h"
#include "sim/object_lifecycle10.h"
#include "shim_impl/disk_filesystem.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI");
}

// The real parsed mesh + its registry key, served to the live LoadAndRegister.
render::Mesh g_realMesh;
std::string  g_realKey;
bool         g_haveReal = false;

// loadMesh hook == VIBE_Mesh_LoadBgfFile @0x5d2348 leg: serve the REAL parsed mesh.
bool ServeRealMesh(const char* /*path*/, const std::string& name, render::Mesh& out) {
    if (!g_haveReal)
        return false;
    out = g_realMesh;
    if (out.name.empty())
        out.name = name;
    return true;
}

// Scan Objects.BIN for the first .bgf that parses with geometry (prefer BUCH, the
// known fast-chunk member: 316 verts / 604 polys / 12 mats).
bool FindRealBgf(app::RealGameAssets& assets) {
    io::ArchiveMount* objs = nullptr;
    for (auto& a : assets.archives)
        if (a.mounted && a.mount &&
            a.name.find("Objects") != std::string::npos) { objs = a.mount.get(); break; }
    if (!objs)
        return false;

    auto tryMember = [&](const io::ArchiveMember& m) -> bool {
        std::string n = m.name;
        for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".bgf") != 0)
            return false;
        std::vector<u8> bytes;
        if (!objs->OpenMember(m.name.c_str(), bytes) || bytes.empty())
            return false;
        std::string key = m.name;
        auto slash = key.find_last_of("/\\");
        if (slash != std::string::npos) key = key.substr(slash + 1);
        auto dot = key.find_last_of('.');
        if (dot != std::string::npos) key = key.substr(0, dot);
        render::Mesh mesh;
        if (!render::LoadBgfFile(bytes.data(), bytes.size(), key, mesh))
            return false;
        if (mesh.polyCount <= 0 || mesh.vertexCount <= 0)
            return false;
        g_realMesh = std::move(mesh);
        g_realKey = key;
        g_haveReal = true;
        return true;
    };

    // Prefer BUCH if present (a real, named, parseable Objects.BIN member).
    for (const auto& m : objs->members()) {
        std::string n = m.name;
        for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n.find("buch.bgf") != std::string::npos && tryMember(m))
            return true;
    }
    int tried = 0;
    for (const auto& m : objs->members()) {
        if (++tried > 256) break;
        if (tryMember(m))
            return true;
    }
    return false;
}

u32 Rd32(const u8* p, int off) { u32 v; std::memcpy(&v, p + off, 4); return v; }
uintptr_t RdPtr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}
float RdF(const u8* p, int off) { float v; std::memcpy(&v, p + off, 4); return v; }

}  // namespace

TEST(ObjectAttachChain, FullChainAttachRealBgf) {
    if (!RealAssetsPresent()) {
        std::printf("    [skip] real game dir absent (set GUILD_GAME_DIR)\n");
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    if (!assets.vfsBound) {
        std::printf("    [skip] VFS not bound\n");
        return;
    }
    if (!FindRealBgf(assets)) {
        std::printf("    [skip] no fast-chunk .bgf member parsed (shipped models are "
                    "the AGF/script chunk variant)\n");
        return;
    }

    // ---- Install the REAL load->attach wiring (the whole chain goes live). ----
    sim::InstallRealObjectAttachWiring();
    // The stock loader's .bgf parse leg == loadMesh: serve the REAL parsed mesh (the
    // "*"-prefixed VFS path resolver is the deferred leaf; the parse pipeline is REAL).
    render::StockObjectHooksMut().loadMesh = &ServeRealMesh;
    render::StockObjectHooksMut().loadTextureSet = nullptr;
    render::StockObjectHooksMut().pathExists = nullptr;
    render::LodModeByteMut() = 0;   // mode 0, LOD disabled (single base frame)

    const int linkBefore = sim::ObjectAttachWiringLinkIntoSceneCalls();
    const int disposeBefore = sim::ObjectAttachWiringDisposeCalls();

    // ---- Drive the full chain. ----
    const float pos[4]      = {12.5f, 3.25f, -7.0f, 0.0f};   // edx (the genuine `pos`)
    const float worldRot[4] = {0.0f, 1.5f, 0.0f, 0.0f};       // ebx (euler; yaw=1.5)
    void* nodeV = sim::ObjectAttachToUniverseNode(
        /*parent=*/nullptr, pos, g_realKey.c_str(), worldRot, /*ctx=*/nullptr);

    // ---- A live, renderable node came back. ----
    CHECK(nodeV != nullptr);
    if (!nodeV) return;
    auto* node = reinterpret_cast<sim::SceneNode10*>(nodeV);
    const u8* nraw = node->raw;

    // Name set (+0) and node type (+533 == 4) by the kind-4 spawn path.
    CHECK_EQ(std::string(reinterpret_cast<const char*>(nraw)), g_realKey);
    CHECK_EQ(static_cast<int>(node->b(sim::n10::kAttachKind)), 4);

    // The stock mesh loaded + found in the stock list (case-insensitive).
    CHECK(render::Registry().FindStockObject(g_realKey.c_str()) != nullptr);
    u8* stock = render::Registry().FindStockObject(g_realKey.c_str());
    CHECK(stock != nullptr);

    // The draw block (node+492) is allocated and populated.
    u8* drawData = reinterpret_cast<u8*>(node->p(sim::n10::kDrawData));
    CHECK(drawData != nullptr);
    if (!drawData) return;

    // The base LOD frame (drawData+244) header has vert/poly counts matching the .bgf,
    // and its stock-ptr resident gate (the shared render::frame slot) is the stock object.
    u8* frame0 = drawData + render::frame::kLodFrameBase;          // drawData + 244
    CHECK_EQ(Rd32(frame0, render::frame::kVertCount),
             static_cast<u32>(g_realMesh.vertexCount));
    CHECK_EQ(Rd32(frame0, render::frame::kPolyCount),
             static_cast<u32>(g_realMesh.polyCount));
    CHECK(Rd32(frame0, render::frame::kVertCount) > 0);
    CHECK(Rd32(frame0, render::frame::kPolyCount) > 0);
    // a2[4] STOCK ptr (the AttachToUniverseNode resident gate) == the registered stock.
    CHECK_EQ(RdPtr(frame0, render::frame::kStockSlot),
             reinterpret_cast<uintptr_t>(stock));
    // The draw arrays (a2[0]/a2[1]) were allocated by AllocPolysAndPoints.
    CHECK(RdPtr(frame0, render::frame::kVertArrPtr) != 0);
    CHECK(RdPtr(frame0, render::frame::kPolyArrPtr) != 0);
    // LOD count bumped to 1 (single base frame in mode 0).
    CHECK(drawData[2316] >= 1);

    // SetPosition applied (node+76/+80/+84 == pos).
    CHECK_EQ(RdF(nraw, 76), pos[0]);
    CHECK_EQ(RdF(nraw, 80), pos[1]);
    CHECK_EQ(RdF(nraw, 84), pos[2]);
    // SetWorldTranslation applied (node+132/+136/+140 == worldRot euler) + the +396
    // frame matrix built (M[15] == 1.0 from VIBE_Math_MatrixFromEuler).
    CHECK_EQ(RdF(nraw, 132), worldRot[0]);
    CHECK_EQ(RdF(nraw, 136), worldRot[1]);
    CHECK_EQ(RdF(nraw, 140), worldRot[2]);
    CHECK_EQ(RdF(nraw, 396 + 15 * 4), 1.0f);

    // LinkIntoScene invoked for the root attach (parent==0); not disposed.
    CHECK_EQ(sim::ObjectAttachWiringLinkIntoSceneCalls(), linkBefore + 1);
    CHECK_EQ(sim::ObjectAttachWiringDisposeCalls(), disposeBefore);

    std::printf("    [info] full chain attached real mesh '%s': %d verts, %d polys, "
                "%d mats; node draw block populated, root linked\n",
                g_realKey.c_str(), g_realMesh.vertexCount, g_realMesh.polyCount,
                g_realMesh.materialCount);

    // Restore inert defaults for any later suite sharing the process.
    render::StockObjectHooksMut().loadMesh = &render::Mesh_LoadByName;
    guild::sim::ObjLife10ResetHooks();
}
