#include "render/mesh_lod_name.h"
#include "tests/framework/test.h"

#include <cstring>
#include <string>
#include <vector>

// =============================================================================
// MeshLodName — golden tests for the stock-object LOD filename strategy
// (VIBE_Mesh_BuildLodFileName @0x5d15fc), the path helper (BuildTexturePath
// @0x5d1034) and the object-node LOD attach (AttachStockObjectLods @0x5d1824).
// Deterministic, no assets: the VFS-existence probe and the scene-graph draw-block
// writes are mocked through MeshLodHooks.
// =============================================================================
namespace {

using namespace guild::render;

// LOD-mode byte layout: low 7 bits = mode (0/1/2), high bit = LOD enabled.
constexpr guild::u8 kEnabled = 0x80;

struct ModeGuard {
    guild::u8 saved;
    ModeGuard(guild::u8 v) : saved(LodModeByteMut()) { LodModeByteMut() = v; }
    ~ModeGuard() { LodModeByteMut() = saved; }
};
struct HookGuard {
    MeshLodHooks saved;
    HookGuard() : saved(MeshLodHooksMut()) {}
    ~HookGuard() { MeshLodHooksMut() = saved; }
};

// ---- BuildTexturePath existence mock (a process-global the C hook reads) ----
std::vector<std::string> g_existing;   // leaf names whose "<name>.bgf" "exists"
bool MockExists(const char* name, const char* suffix) {
    if (std::strcmp(suffix, ".bgf") != 0)
        return false;
    for (const auto& e : g_existing)
        if (e == name)
            return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// BuildTexturePath: composes "*"+name+suffix; existence is the hook.
// ---------------------------------------------------------------------------
TEST(MeshLodName, BuildTexturePath_composes_star_prefixed_path) {
    HookGuard hg;
    std::string path;
    MeshLodHooksMut().textureExists = nullptr;  // inert default -> false
    bool ok = BuildTexturePath("HOUSE", ".bgf", &path);
    CHECK(!ok);                       // no hook -> file absent
    CHECK_EQ(path, std::string("*HOUSE.bgf"));
}

TEST(MeshLodName, BuildTexturePath_existence_via_hook) {
    HookGuard hg;
    g_existing = {"HOUSE_0"};
    MeshLodHooksMut().textureExists = &MockExists;
    CHECK(BuildTexturePath("HOUSE_0", ".bgf", nullptr));
    CHECK(!BuildTexturePath("HOUSE_1", ".bgf", nullptr));
    CHECK(!BuildTexturePath("HOUSE_0", ".raw", nullptr));  // wrong suffix
}

// ---------------------------------------------------------------------------
// lodIndex < 0 : the "_s" variant. Disabled when high bit clear.
// ---------------------------------------------------------------------------
TEST(MeshLodName, BuildLodFileName_s_variant_disabled_when_lod_off) {
    ModeGuard mg(0x00);   // LOD disabled (high bit clear)
    char out[256] = "X", sec[256] = "Y";
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, -1, sec);
    CHECK_EQ(r, (guild::u8)0);        // returns 0, buffers untouched
}

TEST(MeshLodName, BuildLodFileName_s_variant_enabled) {
    ModeGuard mg(kEnabled | 0);       // LOD enabled, mode 0
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, -1, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE_s"));
    CHECK_EQ(std::string(sec), std::string("DIR_s"));
}

TEST(MeshLodName, BuildLodFileName_s_variant_null_second) {
    ModeGuard mg(kEnabled | 0);
    char out[256];
    guild::u8 r = BuildLodFileName("HOUSE", nullptr, out, -1, nullptr);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE_s"));
}

// ---------------------------------------------------------------------------
// lodIndex == 0, mode 0/1 : plain base name.
// ---------------------------------------------------------------------------
TEST(MeshLodName, BuildLodFileName_base_plain_mode0) {
    ModeGuard mg(kEnabled | 0);
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE"));
    CHECK_EQ(std::string(sec), std::string("DIR"));
}

TEST(MeshLodName, BuildLodFileName_base_plain_mode1) {
    ModeGuard mg(kEnabled | 1);
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE"));
    CHECK_EQ(std::string(sec), std::string("DIR"));
}

// ---------------------------------------------------------------------------
// lodIndex > 0 : explicit LOD frame.  name_(i-1); mode-2 flips i -> 2-i.
// ---------------------------------------------------------------------------
TEST(MeshLodName, BuildLodFileName_explicit_lod_mode1) {
    ModeGuard mg(kEnabled | 1);
    char out[256], sec[256];
    BuildLodFileName("HOUSE", "DIR", out, 1, sec);   // v42 = 1-1 = 0
    CHECK_EQ(std::string(out), std::string("HOUSE_0"));
    CHECK_EQ(std::string(sec), std::string("DIR_0"));
    BuildLodFileName("HOUSE", "DIR", out, 2, sec);   // v42 = 2-1 = 1
    CHECK_EQ(std::string(out), std::string("HOUSE_1"));
    CHECK_EQ(std::string(sec), std::string("DIR_1"));
}

TEST(MeshLodName, BuildLodFileName_explicit_lod_mode2_index_flip) {
    ModeGuard mg(kEnabled | 2);                      // switch-LOD: a4 = 2 - a4
    char out[256], sec[256];
    BuildLodFileName("HOUSE", "DIR", out, 1, sec);   // a4=2-1=1, v42=0
    CHECK_EQ(std::string(out), std::string("HOUSE_0"));
    CHECK_EQ(std::string(sec), std::string("DIR_0"));
    BuildLodFileName("HOUSE", "DIR", out, 2, sec);   // a4=2-2=0, v42=-1
    CHECK_EQ(std::string(out), std::string("HOUSE_-1"));
    CHECK_EQ(std::string(sec), std::string("DIR_-1"));
}

// ---------------------------------------------------------------------------
// lodIndex == 0, mode 2 : probe "%s_<n>" downward (n=1, then 0) for an existing
// .bgf; first hit wins. None exist -> plain-name fallback (return 1 if plain
// exists, else 0).
// ---------------------------------------------------------------------------
TEST(MeshLodName, BuildLodFileName_mode2_probe_picks_higher_index) {
    ModeGuard mg(kEnabled | 2);
    HookGuard hg;
    g_existing = {"HOUSE_1", "HOUSE_0"};   // _1 probed first (v7-1 = 1)
    MeshLodHooksMut().textureExists = &MockExists;
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE_1"));
    CHECK_EQ(std::string(sec), std::string("DIR_1"));
}

TEST(MeshLodName, BuildLodFileName_mode2_probe_falls_to_lower_index) {
    ModeGuard mg(kEnabled | 2);
    HookGuard hg;
    g_existing = {"HOUSE_0"};               // only _0 exists; _1 probed and missed
    MeshLodHooksMut().textureExists = &MockExists;
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE_0"));
    CHECK_EQ(std::string(sec), std::string("DIR_0"));
}

TEST(MeshLodName, BuildLodFileName_mode2_probe_plain_fallback) {
    ModeGuard mg(kEnabled | 2);
    HookGuard hg;
    g_existing = {"HOUSE"};                  // neither _1 nor _0; plain HOUSE exists
    MeshLodHooksMut().textureExists = &MockExists;
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)1);
    CHECK_EQ(std::string(out), std::string("HOUSE"));
    CHECK_EQ(std::string(sec), std::string("DIR"));
}

TEST(MeshLodName, BuildLodFileName_mode2_probe_none_exist_returns_0) {
    ModeGuard mg(kEnabled | 2);
    HookGuard hg;
    g_existing.clear();                      // nothing exists at all
    MeshLodHooksMut().textureExists = &MockExists;
    char out[256], sec[256];
    guild::u8 r = BuildLodFileName("HOUSE", "DIR", out, 0, sec);
    CHECK_EQ(r, (guild::u8)0);
}
