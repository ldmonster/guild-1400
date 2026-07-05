// Golden tests for the FLAG / BANNER ("Wimpel") animation cluster:
//   0x4b5d98 AttachFlag, 0x4b5e9c ShowFlag, 0x4b5ef8 RefreshFlagAnimation,
//   0x4b62c0 CollectFlagNodes, plus the heraldry texture-index math and the
//   180° flag-yaw matrix.  See render/cloth_anim.h for the cloth-wave finding
//   (flags are SKELETAL .baf animations, NOT a vertex-wave grid).
#include "test.h"
#include "render/cloth_anim.h"
#include "render/scene_transform.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Trace recorder for the flag-cluster hooks.
struct FlagTrace {
    int   attaches = 0;
    int   texSets  = 0;
    int   animLoads = 0;
    int   parentXforms = 0;
    int   lastTexIndex = -999;
    std::string lastAnimFile;
    float lastMat[9] = {0};
    float lastPos[3] = {0};
    float lastParentPos[3] = {0};
    void* nextHandle = reinterpret_cast<void*>(0x1234);
};

void* h_attach(void* ctx, void* /*personObj*/, const float pos[3]) {
    auto* t = static_cast<FlagTrace*>(ctx);
    ++t->attaches;
    t->lastPos[0] = pos[0]; t->lastPos[1] = pos[1]; t->lastPos[2] = pos[2];
    return t->nextHandle;
}
void h_point(void* /*ctx*/, void* /*node*/, float out[3]) {
    out[0] = 10.f; out[1] = 20.f; out[2] = 30.f;
}
void h_parent(void* ctx, void* /*obj*/, const float pos[3], const float mat[9]) {
    auto* t = static_cast<FlagTrace*>(ctx);
    ++t->parentXforms;
    std::memcpy(t->lastParentPos, pos, sizeof t->lastParentPos);
    std::memcpy(t->lastMat, mat, sizeof t->lastMat);
}
void h_tex(void* ctx, void* /*obj*/, int texIndex, void* /*player*/) {
    auto* t = static_cast<FlagTrace*>(ctx);
    ++t->texSets;
    t->lastTexIndex = texIndex;
}
void h_anim(void* ctx, void* /*obj*/, const char* file, int /*mode*/) {
    auto* t = static_cast<FlagTrace*>(ctx);
    ++t->animLoads;
    t->lastAnimFile = file;
}

FlagAnimHooks makeHooks(FlagTrace& t) {
    FlagAnimHooks H{};
    H.ctx = &t;
    H.pointThroughBoneChain = h_point;
    H.attachToUniverseNode  = h_attach;
    H.applyParentTransform  = h_parent;
    H.selectTextureSet      = h_tex;
    H.loadObjectAnimation   = h_anim;
    return H;  // strCmpNoCase left null -> default case-insensitive compare
}

} // namespace

// --- heraldry texture-index math (0x4b5e3f, -62 bias) ------------------------
TEST(cloth_anim, heraldry_texture_index_bias) {
    CHECK_EQ(FlagTextureSetIndex(62), 0);    // 62 - 62
    CHECK_EQ(FlagTextureSetIndex(63), 1);
    CHECK_EQ(FlagTextureSetIndex(100), 38);  // 100 - 62
    CHECK_EQ(FlagTextureSetIndex(62 + 7), 7);
}

// --- 180° flag yaw matrix (euler {0,π,0}, 0x4b5df2 / 0x4b5e0c) ----------------
TEST(cloth_anim, flag_yaw_is_180_degrees) {
    const float euler[3] = {0.f, kFlagYawPi, 0.f};
    Mat3 R = MatrixFromEuler(euler);
    // MatrixFromEuler: row0 = [cy*cz, ...], row2 = [-sy, sx*cy, cx*cy].
    // ey=π -> cy=-1, sy~0, ex=ez=0 -> cx=cz=1, sx=sz=0.
    // => m[0]=cy*cz=-1, m[8]=cx*cy=-1, m[4]=cx*cz=1 (the classic 180° yaw).
    CHECK(std::fabs(R.m[0] - (-1.f)) < 1e-5f);  // x flips
    CHECK(std::fabs(R.m[4] - ( 1.f)) < 1e-5f);  // y stays
    CHECK(std::fabs(R.m[8] - (-1.f)) < 1e-5f);  // z flips
    // pi constant matches the binary's 0x40490FDB.
    CHECK(std::fabs(kFlagYawPi - 3.14159274101257324f) < 1e-12f);
}

// --- AttachFlag: matches "dummy_FAHNE" and runs the full attach (0x4b5d98) ----
TEST(cloth_anim, attach_flag_full_path) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = 3;
    p.heraldryByte = 70;  // texIndex = 70 - 62 = 8
    FlagObject out{};

    bool keepWalking = AttachFlag(H, "dummy_FAHNE", reinterpret_cast<void*>(0xBB), p,
                                  reinterpret_cast<void*>(0xCC), out);
    CHECK(keepWalking);
    CHECK_EQ(t.attaches, 1);
    CHECK_EQ(t.parentXforms, 1);
    CHECK_EQ(t.texSets, 1);
    CHECK_EQ(t.lastTexIndex, 8);
    CHECK_EQ(t.animLoads, 1);
    CHECK(t.lastAnimFile == "sonstiges\\sp_WIMPEL.baf");
    // attach point came through PointThroughBoneChain.
    CHECK(std::fabs(t.lastPos[0] - 10.f) < 1e-6f);
    // object flag bookkeeping (0x4b5e58 / 0x4b5e76 / 0x4b5e8a).
    CHECK_EQ((int)out.texSetState, 4);
    CHECK_EQ((int)out.renderFlags530, 0x44);   // (0 & 0xB3) | 0x44
    CHECK_EQ((int)out.renderFlags529, 0x00);
    CHECK_EQ(out.texSetIndexApplied, 8);
    CHECK_EQ(out.handle, t.nextHandle);
}

// --- AttachFlag: no heraldry skips the texture set but still animates ---------
TEST(cloth_anim, attach_flag_no_heraldry) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = kNoHeraldry;  // 0xFFFF
    FlagObject out{};

    AttachFlag(H, "dummy_FAHNE", nullptr, p, nullptr, out);
    CHECK_EQ(t.attaches, 1);
    CHECK_EQ(t.texSets, 0);          // skipped (0x4b5e24)
    CHECK_EQ(t.animLoads, 1);        // animation still loads (0x4b5e6b)
    CHECK_EQ((int)out.texSetState, 0);
    CHECK_EQ(out.texSetIndexApplied, -1);
    CHECK_EQ((int)out.renderFlags530, 0x44);  // still set
}

// --- AttachFlag: non-"dummy_FAHNE" node is a no-op (0x4b5da6) -----------------
TEST(cloth_anim, attach_flag_ignores_other_nodes) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = 1; p.heraldryByte = 65;
    FlagObject out{};

    bool keep = AttachFlag(H, "some_other_bone", nullptr, p, nullptr, out);
    CHECK(keep);
    CHECK_EQ(t.attaches, 0);
    CHECK_EQ(t.animLoads, 0);
    // case-insensitive: "DUMMY_FAHNE" still matches.
    bool keep2 = AttachFlag(H, "DUMMY_FAHNE", nullptr, p, nullptr, out);
    CHECK(keep2);
    CHECK_EQ(t.attaches, 1);
}

// --- ShowFlag: re-applies heraldry on "sp_WIMPEL" only (0x4b5e9c) ------------
TEST(cloth_anim, show_flag_reapplies_texture) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.heraldry = 5; p.heraldryByte = 80;  // texIndex = 18
    FlagObject obj{}; obj.handle = reinterpret_cast<void*>(0xEE);

    CHECK(ShowFlag(H, "sp_WIMPEL", obj, p, nullptr));
    CHECK_EQ(t.texSets, 1);
    CHECK_EQ(t.lastTexIndex, 18);
    CHECK_EQ(t.attaches, 0);   // no re-attach
    CHECK_EQ(t.animLoads, 0);  // no re-animate

    // wrong node name -> no-op.
    ShowFlag(H, "dummy_FAHNE", obj, p, nullptr);
    CHECK_EQ(t.texSets, 1);

    // no heraldry -> no-op even on "sp_WIMPEL".
    p.heraldry = kNoHeraldry;
    ShowFlag(H, "sp_WIMPEL", obj, p, nullptr);
    CHECK_EQ(t.texSets, 1);
}

// --- RefreshFlagAnimation gate: build type must be 5/6/7 (0x4b5f3b) ----------
TEST(cloth_anim, refresh_build_type_gate) {
    CHECK(FlagBuildTypeEnabled(5));
    CHECK(FlagBuildTypeEnabled(6));
    CHECK(FlagBuildTypeEnabled(7));
    CHECK(!FlagBuildTypeEnabled(4));
    CHECK(!FlagBuildTypeEnabled(8));
    CHECK(!FlagBuildTypeEnabled(0));
}

TEST(cloth_anim, refresh_flag_animation_walks_nodes) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = 2; p.heraldryByte = 64;  // texIndex 2

    std::vector<FlagSceneNode> nodes = {
        {"dummy_FAHNE", reinterpret_cast<void*>(0x1)},
        {"torso",       reinterpret_cast<void*>(0x2)},
        {"dummy_FAHNE", reinterpret_cast<void*>(0x3)},
    };
    std::vector<FlagObject> produced(nodes.size());

    bool ran = RefreshFlagAnimation(H, p, /*buildType*/6, /*nodeFlagBit528*/true,
                                    nullptr, nodes.data(), (int)nodes.size(),
                                    produced.data());
    CHECK(ran);
    CHECK_EQ(t.attaches, 2);     // two dummy_FAHNE nodes
    CHECK_EQ(t.animLoads, 2);
    CHECK(produced[0].animLoaded);
    CHECK(!produced[1].animLoaded);  // "torso" skipped
    CHECK(produced[2].animLoaded);

    // gate fails: build type 4 -> no work.
    FlagTrace t2{}; FlagAnimHooks H2 = makeHooks(t2);
    CHECK(!RefreshFlagAnimation(H2, p, 4, true, nullptr, nodes.data(),
                                (int)nodes.size(), produced.data()));
    CHECK_EQ(t2.attaches, 0);

    // gate fails: no universe node.
    FlagPerson p2 = p; p2.universeNode = nullptr;
    CHECK(!RefreshFlagAnimation(H2, p2, 6, true, nullptr, nodes.data(),
                                (int)nodes.size(), produced.data()));

    // gate fails: no heraldry.
    FlagPerson p3 = p; p3.heraldry = kNoHeraldry;
    CHECK(!RefreshFlagAnimation(H2, p3, 6, true, nullptr, nodes.data(),
                                (int)nodes.size(), produced.data()));
}

// --- CollectFlagNodes: gathers "sp_WIMPEL"-named nodes, caps at 32 (0x4b62c0) -
TEST(cloth_anim, collect_flag_nodes_substring_and_cap) {
    void* out[kMaxFlagNodes] = {nullptr};
    int count = 0;

    // substring match (the name contains "sp_WIMPEL").
    CHECK(CollectFlagNodes("char_sp_WIMPEL_01", reinterpret_cast<void*>(0xA),
                           out, kMaxFlagNodes, count));
    CHECK_EQ(count, 1);
    CHECK_EQ(out[0], reinterpret_cast<void*>(0xA));

    // non-matching node: count unchanged, still keep walking.
    CHECK(CollectFlagNodes("torso", reinterpret_cast<void*>(0xB),
                           out, kMaxFlagNodes, count));
    CHECK_EQ(count, 1);

    CHECK(CollectFlagNodes("sp_WIMPEL", reinterpret_cast<void*>(0xC),
                           out, kMaxFlagNodes, count));
    CHECK_EQ(count, 2);
    CHECK_EQ(out[1], reinterpret_cast<void*>(0xC));

    // fill to the cap; the 32nd insertion makes count==32 -> returns false (stop).
    int filler = 0;
    bool keep = true;
    while (count < kMaxFlagNodes)
        keep = CollectFlagNodes("sp_WIMPEL_x", reinterpret_cast<void*>(0x100 + filler++),
                                out, kMaxFlagNodes, count);
    CHECK_EQ(count, kMaxFlagNodes);
    CHECK(!keep);  // 0x4b62dd: count < 32 is now false
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / boundary coverage (ASAN+UBSAN).
// =============================================================================

// (w1) RefreshFlagAnimation with ZERO nodes: the gate passes but the walk loop
//      iterates zero times -> no AttachFlag, no deref of nodes/produced. Pass a
//      null nodes array with nodeCount 0 to prove the loop never touches it.
TEST(cloth_anim, harden_refresh_zero_nodes) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = 2; p.heraldryByte = 64;

    bool ran = RefreshFlagAnimation(H, p, /*buildType*/6, /*nodeFlagBit528*/true,
                                    nullptr, /*nodes*/nullptr, /*nodeCount*/0,
                                    /*produced*/nullptr);
    CHECK(ran);                  // gate passed
    CHECK_EQ(t.attaches, 0);     // no nodes -> no attach
    CHECK_EQ(t.animLoads, 0);
}

// (w2) RefreshFlagAnimation with produced == null but nodeCount > 0: AttachFlag
//      still runs per node, and the `if (produced)` guard must skip the write-back
//      (no null deref of produced[i]).
TEST(cloth_anim, harden_refresh_null_produced) {
    FlagTrace t{};
    FlagAnimHooks H = makeHooks(t);
    FlagPerson p{};
    p.universeNode = reinterpret_cast<void*>(0xAA);
    p.heraldry = 1; p.heraldryByte = 70;
    std::vector<FlagSceneNode> nodes = {
        {"dummy_FAHNE", reinterpret_cast<void*>(0x1)},
        {"dummy_FAHNE", reinterpret_cast<void*>(0x2)},
    };
    bool ran = RefreshFlagAnimation(H, p, 7, true, nullptr, nodes.data(),
                                    (int)nodes.size(), /*produced*/nullptr);
    CHECK(ran);
    CHECK_EQ(t.attaches, 2);     // both attached; produced write-back skipped safely
}

// (w3) CollectFlagNodes at the 32-node CAP: writing the 32nd entry fills the last
//      valid slot (index 31) and returns false; any further match must NOT write
//      past `outCapacity` (the `slot < outCapacity` guard). We size `out` to EXACTLY
//      kMaxFlagNodes so ASAN catches any write at index >= 32.
TEST(cloth_anim, harden_collect_cap_no_overflow) {
    void* out[kMaxFlagNodes] = {nullptr};
    int count = 0;
    bool keep = true;
    // Insert exactly kMaxFlagNodes matching nodes; the last makes count==32 -> false.
    for (int i = 0; i < kMaxFlagNodes; ++i)
        keep = CollectFlagNodes("sp_WIMPEL", reinterpret_cast<void*>(0x100 + i),
                                out, kMaxFlagNodes, count);
    CHECK_EQ(count, kMaxFlagNodes);
    CHECK(!keep);
    CHECK_EQ(out[kMaxFlagNodes - 1], reinterpret_cast<void*>(0x100 + kMaxFlagNodes - 1));

    // A further match: count keeps incrementing (the engine's acc[0]++) but the
    // slot is now >= outCapacity, so the `slot < outCapacity` guard MUST prevent
    // any out-of-bounds write into out[] (ASAN would trap otherwise).
    keep = CollectFlagNodes("sp_WIMPEL", reinterpret_cast<void*>(0xDEAD),
                            out, kMaxFlagNodes, count);
    CHECK_EQ(count, kMaxFlagNodes + 1);   // count still advances
    CHECK(!keep);                          // still past the cap

    // Null out buffer: the `out &&` guard prevents any write even on a match.
    int c2 = 0;
    CollectFlagNodes("sp_WIMPEL", reinterpret_cast<void*>(0xBEEF), nullptr, 0, c2);
    CHECK_EQ(c2, 1);                        // count advanced; no write attempted
}

// (w4) CollectFlagNodes with a null node name: NameContains guards null -> no match.
TEST(cloth_anim, harden_collect_null_name) {
    void* out[4] = {nullptr};
    int count = 0;
    bool keep = CollectFlagNodes(nullptr, reinterpret_cast<void*>(0x1), out, 4, count);
    CHECK(keep);                 // count < 32 still true
    CHECK_EQ(count, 0);          // null name -> no match, nothing written
    CHECK_EQ(out[0], nullptr);
}

// --- NameContains predicate (0x5cb930 strstr behavior) -----------------------
TEST(cloth_anim, name_contains_predicate) {
    CHECK(NameContains("abc_sp_WIMPEL", "sp_WIMPEL"));
    CHECK(NameContains("sp_WIMPEL", "sp_WIMPEL"));
    CHECK(!NameContains("sp_WIMPE", "sp_WIMPEL"));
    CHECK(NameContains("anything", ""));   // empty needle matches (0x5cb93c)
    CHECK(!NameContains(nullptr, "x"));
}
