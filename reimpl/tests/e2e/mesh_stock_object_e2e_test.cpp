#include "test.h"

// GUARDED real-asset e2e: register a REAL parsed .bgf mesh into the stock-object
// registry through VIBE_Mesh_LoadAndRegister (0x5d32d4) and assert
// VIBE_Mesh_FindStockObject (0x5d10d0) finds it and the consumer offsets are sane.
//
//   1. mount the real game assets (Gilde.INI + Resources/*.BIN), bind the VFS,
//   2. scan Objects.BIN for a .bgf member that parses via the REAL pipeline
//      (render::LoadBgfFile — the same post-process Mesh_LoadByName @0x5d2348 runs),
//   3. feed that real parsed Mesh to StockRegistry::LoadAndRegister (REUSE), so the
//      record's vert/poly arrays reference the REAL parsed geometry,
//   4. assert FindStockObject locates it by name (case-insensitive) and the
//      +540/+548/+556 array pointers + +68/+76/+480 counts are consistent with the
//      parsed mesh, and the method-ptr table is set.
//
// GUARDED: clean skip (zero checks) when the real game dir is absent. The shipped
// objects are predominantly the AGF/script chunk variant rather than the fast-chunk
// magic the buffer reader handles; if NO member parses with geometry the test reports
// and returns cleanly (it never fakes a mesh — rule 8). Honors GUILD_GAME_DIR.
#include "app/real_boot.h"
#include "io/vfs.h"
#include "render/mesh_asset.h"
#include "render/mesh_load.h"
#include "render/mesh_stock_object.h"
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
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI");
}

// The Mesh the e2e hands LoadAndRegister: the real parsed mesh found in Objects.BIN.
render::Mesh g_realMesh;
bool g_haveReal = false;

bool ServeReal(const char* /*path*/, const std::string& name, render::Mesh& out) {
    if (!g_haveReal)
        return false;
    out = g_realMesh;
    if (out.name.empty())
        out.name = name;
    return true;
}

// Scan Objects.BIN for the first .bgf member that parses with geometry.
bool FindRealBgf(app::RealGameAssets& assets) {
    io::ArchiveMount* objs = nullptr;
    for (auto& a : assets.archives)
        if (a.mounted && a.mount &&
            a.name.find("Objects") != std::string::npos) { objs = a.mount.get(); break; }
    if (!objs)
        return false;

    int tried = 0;
    for (const auto& m : objs->members()) {
        std::string n = m.name;
        for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".bgf") != 0)
            continue;
        if (++tried > 128)
            break;
        std::vector<u8> bytes;
        if (!objs->OpenMember(m.name.c_str(), bytes) || bytes.empty())
            continue;
        render::Mesh mesh;
        // strip the extension for the stored name (the registry key).
        std::string key = m.name;
        auto slash = key.find_last_of("/\\");
        if (slash != std::string::npos) key = key.substr(slash + 1);
        auto dot = key.find_last_of('.');
        if (dot != std::string::npos) key = key.substr(0, dot);
        if (!render::LoadBgfFile(bytes.data(), bytes.size(), key, mesh))
            continue;
        if (mesh.polyCount <= 0 || mesh.vertexCount <= 0)
            continue;
        g_realMesh = std::move(mesh);
        g_haveReal = true;
        return true;
    }
    return false;
}

u32 Rd32(const u8* p, int off) { u32 v; std::memcpy(&v, p + off, 4); return v; }
uintptr_t RdPtr(const u8* p, int off) {
    uintptr_t v; std::memcpy(&v, p + off, sizeof(v)); return v;
}

}  // namespace

TEST(MeshStockObject, E2eRealBgfRegisterAndFind) {
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

    // Drive LoadAndRegister via the real parsed mesh.
    render::StockObjectHooksMut().loadMesh = &ServeReal;
    render::StockObjectHooksMut().loadTextureSet = nullptr;
    render::StockObjectHooksMut().pathExists = nullptr;

    render::StockRegistry reg;
    const std::string key = g_realMesh.name;
    u8* stock = reg.LoadAndRegister(key.c_str(), "OBJ");
    CHECK(stock != nullptr);
    if (!stock) return;

    // counts mirror the parsed mesh.
    CHECK_EQ(Rd32(stock, render::stockrec::kVertCount),
             static_cast<u32>(g_realMesh.vertexCount));
    CHECK_EQ(Rd32(stock, render::stockrec::kPolyCount),
             static_cast<u32>(g_realMesh.polyCount));
    CHECK_EQ(Rd32(stock, render::stockrec::kMatCount),
             static_cast<u32>(g_realMesh.materialCount));
    CHECK_EQ(Rd32(stock, render::stockrec::kRefCount), 1u);

    // array pointers are non-null and the method table is set.
    CHECK(RdPtr(stock, render::stockrec::kVertArray) != 0);
    CHECK(RdPtr(stock, render::stockrec::kPolyArray) != 0);
    CHECK_EQ(Rd32(stock, render::stockrec::kMethodDelete),
             (u32)render::kMethodDeleteStockObject);

    // FindStockObject locates it by name (case-insensitive 63-char).
    CHECK(reg.FindStockObject(key.c_str()) == stock);
    std::string lower = key;
    for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
    CHECK(reg.FindStockObject(lower.c_str()) == stock);
    CHECK(reg.FindStockObject("DEFINITELY_NOT_A_MESH") == nullptr);

    // sanity: first stock poly's vertex indices are in range of the vertex count.
    u8* polyArr = reinterpret_cast<u8*>(RdPtr(stock, render::stockrec::kPolyArray));
    if (polyArr) {
        u32 idx0 = Rd32(polyArr, 24);  // spoly +24 first vertex index
        CHECK(idx0 < static_cast<u32>(g_realMesh.vertexCount + 8));
    }

    std::printf("    [info] registered real mesh '%s': %d verts, %d polys, %d mats\n",
                key.c_str(), g_realMesh.vertexCount, g_realMesh.polyCount,
                g_realMesh.materialCount);

    // restore inert default loadMesh for other suites.
    render::StockObjectHooksMut().loadMesh = &render::Mesh_LoadByName;
}
