#include "render/mesh_lod_name.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

// =============================================================================
// MeshAttachLods — golden tests for VIBE_Mesh_AttachStockObjectLods @0x5d1824.
// The scene-graph draw-block writes (AllocDrawData / AttachStockTextures) are
// recorded through MeshLodHooks; we assert the exact sequence of (drawBlockOffset,
// stockName) attaches the original performs for each LOD mode.
// =============================================================================
namespace {

using namespace guild::render;

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

struct AttachRec { int off; int lodArg; std::string name; };
std::vector<AttachRec> g_attaches;
int g_allocCalls = 0;

void RecAlloc(void*) { ++g_allocCalls; }
bool RecAttach(void*, int off, int lodArg, const char* name) {
    g_attaches.push_back({off, lodArg, name ? name : ""});
    return true;  // "stock object found+attached"
}

void InstallRecorder() {
    g_attaches.clear();
    g_allocCalls = 0;
    MeshLodHooksMut().allocDrawData = &RecAlloc;
    MeshLodHooksMut().attachStockTextures = &RecAttach;
}

} // namespace

// Mode 0 (LOD enabled): base @244 + "_s" @1396, no LOD frames.
TEST(MeshAttachLods, mode0_base_plus_s_variant) {
    ModeGuard mg(kEnabled | 0);
    HookGuard hg;
    InstallRecorder();

    AttachStockObjectLods(nullptr, /*attachExisting=*/false, "HOUSE", 7);

    CHECK_EQ(g_allocCalls, 1);
    CHECK_EQ(g_attaches.size(), (size_t)2);
    CHECK_EQ(g_attaches[0].off, 244);
    CHECK_EQ(g_attaches[0].name, std::string("HOUSE"));
    CHECK_EQ(g_attaches[1].off, 1396);
    CHECK_EQ(g_attaches[1].name, std::string("HOUSE_s"));
}

// LOD disabled: base @244 only (no "_s", BuildLodFileName(-1) -> 0).
TEST(MeshAttachLods, lod_disabled_base_only) {
    ModeGuard mg(0x00);
    HookGuard hg;
    InstallRecorder();

    AttachStockObjectLods(nullptr, false, "HOUSE", 0);

    CHECK_EQ(g_attaches.size(), (size_t)1);
    CHECK_EQ(g_attaches[0].off, 244);
    CHECK_EQ(g_attaches[0].name, std::string("HOUSE"));
}

// Mode 1 (multi-LOD): base @244, "_s" @1396, LOD1 @244+384, LOD2 @244+768.
TEST(MeshAttachLods, mode1_attaches_lod_frames) {
    ModeGuard mg(kEnabled | 1);
    HookGuard hg;
    InstallRecorder();

    AttachStockObjectLods(nullptr, false, "HOUSE", 0);

    // base + _s + LOD1 + LOD2
    CHECK_EQ(g_attaches.size(), (size_t)4);
    CHECK_EQ(g_attaches[0].off, 244);
    CHECK_EQ(g_attaches[0].name, std::string("HOUSE"));
    CHECK_EQ(g_attaches[1].off, 1396);
    CHECK_EQ(g_attaches[1].name, std::string("HOUSE_s"));
    // LOD frame 1: drawData + 384 + 244 = 628, name HOUSE_0 (v42 = 1-1).
    CHECK_EQ(g_attaches[2].off, 628);
    CHECK_EQ(g_attaches[2].name, std::string("HOUSE_0"));
    // LOD frame 2: drawData + 768 + 244 = 1012, name HOUSE_1 (v42 = 2-1).
    CHECK_EQ(g_attaches[3].off, 1012);
    CHECK_EQ(g_attaches[3].name, std::string("HOUSE_1"));
}

// Fast path (attachExisting): attach the named object directly at @244.
TEST(MeshAttachLods, attach_existing_fast_path) {
    ModeGuard mg(kEnabled | 1);
    HookGuard hg;
    InstallRecorder();

    AttachStockObjectLods(nullptr, /*attachExisting=*/true, "DOOR", 3);

    CHECK_EQ(g_allocCalls, 1);
    CHECK_EQ(g_attaches.size(), (size_t)1);
    CHECK_EQ(g_attaches[0].off, 244);
    CHECK_EQ(g_attaches[0].lodArg, 3);
    CHECK_EQ(g_attaches[0].name, std::string("DOOR"));
}
