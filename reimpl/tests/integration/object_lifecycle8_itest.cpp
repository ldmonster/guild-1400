#include "test.h"

// Integration: drive object_lifecycle8's room-assignment and avatar-parse leaves
// against their REAL reconstructed string/math siblings — NOT mocks. In the live
// binary VIBE_Object_AssignToRoomByName (gilde.exe 0x5775ac) calls
// VIBE_Util_StrCmpNoCaseN (string_ops.cpp @0x5e0db0) and VIBE_Math_VectorWithinTolerance
// (math.cpp @0x5caa4c) directly; VIBE_Object_ParseAndAttachAvatar (0x4fffd4) calls
// VIBE_Util_StrChr (string_ops.cpp @0x5d3ef0, strrchr semantics) and
// VIBE_Util_StrCmpNoCase (string_ops.cpp @0x5cb8f0). object_lifecycle8.cpp already
// links those real functions internally (no hook indirection) — exactly the live
// wiring. Here we exercise the cross-module flow end to end and assert the real
// case-folding / tolerance / strrchr behaviour drives the assignment.
#include <cstring>

#include "sim/object_lifecycle8.h"
#include "util/string_ops.h"   // REAL reconstructed siblings used inside the module
#include "util/math.h"

using namespace guild;
using namespace guild::sim;

// Sanity: confirm the real siblings behave as the module relies on (case-insensitive
// prefix compare, per-component tolerance), so the cross-module assertions below are
// meaningful and not accidentally passing on a stub.
TEST(ObjLife8_Itest, RealSiblingsBehaveAsRelied) {
    // util::StrCmpNoCaseN folds case; equal prefix within n -> 0.
    CHECK_EQ(util::StrCmpNoCaseN("Bedroom_01", "bedroom", 7), 0);
    CHECK(util::StrCmpNoCaseN("kitchen", "bedroom", 7) != 0);
    // util::VectorWithinTolerance is per-component |a-b| <= tol.
    float a[3] = {10, 20, 30}, b[3] = {10, 20, 30};
    CHECK(util::VectorWithinTolerance(a, b, 100.0f));
    float c[3] = {500, 0, 0}, d[3] = {0, 0, 0};
    CHECK(!util::VectorWithinTolerance(c, d, 100.0f));
    // util::StrChrLast == strrchr (last match), as the avatar parse relies on.
    char s[] = "av_human_male";
    char* last = util::StrChrLast(s, '_');
    CHECK(last != nullptr);
    if (last) CHECK_EQ((int)(last - s), 8);   // the SECOND underscore
}

// AssignToRoomByName driven entirely through the real StrCmpNoCaseN + tolerance:
// a case-mismatched-but-equal name + an anchor within 100 units assigns the node.
TEST(ObjLife8_Itest, AssignRoomThroughRealSiblings) {
    ObjLife8ResetHooks();                         // identity bone-chain (inert default)
    SceneNode8 node;
    std::strcpy(node.s(0), "BEDROOM_ground");     // upper-case; real fold matches "bedroom"
    node.f(76) = 5.f; node.f(80) = 6.f; node.f(84) = 7.f;
    float anchor[3] = {55.f, 6.f, 7.f};           // dx=50 <= 100 -> within tolerance
    SceneNode8* out = nullptr;
    char r = ObjectAssignToRoomByName(&node, "bedroom", anchor, &out);
    CHECK_EQ((int)r, 0);                           // assigned
    CHECK(out == &node);

    // Same name, anchor just out of tolerance on one axis -> real VectorWithinTolerance
    // rejects, function returns 1 and leaves the room unassigned.
    SceneNode8 node2;
    std::strcpy(node2.s(0), "bedroom_upper");
    node2.f(76) = 0.f; node2.f(80) = 0.f; node2.f(84) = 0.f;
    float anchor2[3] = {0.f, 200.f, 0.f};          // dy=200 > 100
    SceneNode8* out2 = nullptr;
    char r2 = ObjectAssignToRoomByName(&node2, "bedroom", anchor2, &out2);
    CHECK_EQ((int)r2, 1);
    CHECK(out2 == nullptr);
}

// ParseAndAttachAvatar: the kind-71 path uses the real StrChrLast to split the
// name after the LAST '_' and the real StrCmpNoCase to match the avatar table.
// We supply a real 2-row x 756-byte table and a Gebaeude record via the inert
// hook so the cross-link is observable; the string match itself runs on real code.
TEST(ObjLife8_Itest, AvatarParseRealStringMatch) {
    static u8 g_gebaeude[256];
    std::memset(g_gebaeude, 0, sizeof(g_gebaeude));

    ObjLife8Hooks h;                               // mostly inert
    h.parseNameAndBind = [](SceneNode8*, void*) { return 1; };
    h.buildingCreateGebaeude = [](unsigned) -> void* { return g_gebaeude; };
    ObjLife8SetHooks(h);

    SceneNode8 node;
    std::strcpy(node.s(0), "av_male");             // token after last '_' == "male"
    node.d(72) = 0x02000047;                        // high byte 2 (building), low 0x47 == 71
    node.b(533) = 1;                                // attachKind set (the building branch)

    // avatar table: row0 "female", row1 "male" (real StrCmpNoCase folds case).
    char table[2 * 756];
    std::memset(table, 0, sizeof(table));
    std::strcpy(table + 0,   "female");
    std::strcpy(table + 756, "Male");               // mixed case -> real fold matches "male"

    int r = ObjectParseAndAttachAvatar(&node, nullptr, table, 2);
    (void)r;
    // The real StrCmpNoCase fold matched row 1 ("Male" ~= "male"): the module copies
    // the matched entry name into the Gebaeude at +5 — assert that real-string match.
    // (The original's row index at +101 and back-ptr at +97 are adjacent 4-byte slots
    // in the 32-bit record; on LP64 the 8-byte back-ptr at +97 structurally overlaps
    // +101, so we verify the row match via the +5 name copy, which does not overlap.)
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(g_gebaeude + 5), "Male"), 0);
    CHECK(*reinterpret_cast<SceneNode8**>(g_gebaeude + 97) == &node);
    // The non-building inherit path: a node whose attachKind is 0 inherits the
    // parent's +512 owner link (real control-flow tail).
    SceneNode8 parent; *reinterpret_cast<void**>(parent.raw + 512) = reinterpret_cast<void*>(0xBEEF);
    SceneNode8 leaf;   leaf.b(533) = 0;
    *reinterpret_cast<SceneNode8**>(leaf.raw + 504) = &parent;
    ObjectParseAndAttachAvatar(&leaf, nullptr, nullptr, 0);
    CHECK(*reinterpret_cast<void**>(leaf.raw + 512) == reinterpret_cast<void*>(0xBEEF));
    ObjLife8ResetHooks();
}
