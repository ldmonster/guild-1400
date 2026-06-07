#include "test.h"

// Integration: drive object_lifecycle9 against REAL reconstructed siblings, not
// mocks, exactly as the live binary wires them:
//
//   * ParseNameAndBind (0x4ffb40) calls the REAL util::StrCmpNoCase (string_ops.cpp
//     @0x5cb8f0 == VIBE_Util_StrCmpNoCase) and util::StrChrLast (string_ops.cpp
//     @0x5d3ef0 == VIBE_Util_StrChr) directly — the same calls the original makes.
//     We assert the case-insensitive table match the live game performs.
//   * RunScriptCallback (0x5b34e4) forwards its terminal dirty-mark into the REAL
//     ObjectMarkDirtyFlag (object_lifecycle7.cpp @0x5af298 == VIBE_Object_MarkDirtyFlag),
//     the genuine VIBE_SceneGraph_WalkAndInvoke callback the original passes.
//     We assert the real sibling sets the node's +528 dirty bit.
//   * The utilStrCmp hook is forwarded into the REAL util::StrCmpNoCase as the
//     plain comparator stand-in, exercising the cross-module call site live.
#include "sim/object_lifecycle9.h"
#include "sim/object_lifecycle7.h"   // REAL sibling: ObjectMarkDirtyFlag
#include "util/string_ops.h"         // REAL siblings: StrCmpNoCase, StrChrLast

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
// Forward the unreconstructed-plain-StrCmp hook into the REAL case-insensitive
// comparator (a faithful cross-module wiring through util::string_ops).
int RealStrCmpForward(const char* a, const char* b) {
    return guild::util::StrCmpNoCase(a, b);
}
}  // namespace

// ParseNameAndBind's case-insensitive suffix match runs through the REAL
// util::StrCmpNoCase: a mixed-case node name must still bind to the lower-case
// table row, proving the live stricmp wiring.
TEST(ObjLifecycle9Itest, ParseNameUsesRealStrCmpNoCaseForCaseInsensitiveBind) {
    std::vector<u8> sceneType(kSceneTypeRows * kSceneTypeStride, 0);
    std::vector<u8> building(kBuildingRows * kBuildingStride, 0);
    // table row 9 name (lower-case) = "lantern".
    std::strcpy(reinterpret_cast<char*>(&sceneType[9 * kSceneTypeStride + 1]), "lantern");

    ObjLife9Hooks h{};
    h.utilStrCmp = &RealStrCmpForward;   // wire the plain-cmp hook to REAL util sibling
    ObjLife9SetHooks(h);

    ParseTables9 pt;
    pt.sceneTypeBase = sceneType.data();
    pt.buildingBase  = building.data();
    pt.markerScene   = "ob";
    pt.markerBuild   = "gb";

    i32 word = 0;
    // Mixed-case suffix "LANTERN" must match lower-case row 9 via real StrCmpNoCase.
    int rc = ObjectParseNameAndBind("ob_LANTERN", &word, pt);
    CHECK_EQ(rc, 1);
    CHECK_EQ(word, (i32)(0x1000000 | 9));

    // Confirm the REAL util siblings independently behave as the binding relies on.
    CHECK_EQ(guild::util::StrCmpNoCase("LANTERN", "lantern"), 0);
    char probe[] = "a_b_c";
    char* last = guild::util::StrChrLast(probe, '_');
    CHECK(last != nullptr);
    if (last) CHECK_EQ((int)(last - probe), 3);   // last '_' is at index 3

    ObjLife9ResetHooks();
}

// RunScriptCallback's terminal scene-graph walk fires the REAL ObjectMarkDirtyFlag
// (object_lifecycle7) on the node, setting +528 bit2 and clearing +531 bit0 —
// exactly the side effect the live VIBE_Object_MarkDirtyFlag produces.
TEST(ObjLifecycle9Itest, RunScriptCallbackDirtyMarkRunsRealMarkDirtyFlag) {
    SceneNode9 node;
    node.b(528) = 0;
    node.b(531) = 0x01;   // bit0 set; the real MarkDirtyFlag must clear it
    node.b(530) = 0x80;   // bit7 set

    ScriptCbInputs9 in;
    in.scriptName = "obj_new_mesh";
    in.triggerByte = 0;
    in.ctxOwnerTop = 3;
    in.nodeOwnerTop = 3;
    in.scriptBlockOk = 1;

    ObjLife9Hooks h{};   // all leaves inert; only the REAL MarkDirtyFlag fires.
    ObjLife9SetHooks(h);

    CHECK_EQ((int)ObjectRunScriptCallback(&node, in), 1);
    // REAL ObjectMarkDirtyFlag(node, clearHi=1): +528 |= 4, +530 &= ~0x80, +531 &= ~1.
    CHECK_EQ((int)(node.b(528) & 4), 4);
    CHECK_EQ((int)(node.b(530) & 0x80), 0);
    CHECK_EQ((int)(node.b(531) & 1), 0);

    // Independently confirm the real sibling's contract on a fresh node.
    SceneNode7 probe;
    probe.b(528) = 0; probe.b(531) = 0xFF; probe.b(530) = 0xFF;
    char rc = ObjectMarkDirtyFlag(&probe, /*clearHi=*/1);
    CHECK_EQ((int)rc, 1);
    CHECK_EQ((int)(probe.b(528) & 4), 4);
    CHECK_EQ((int)(probe.b(530) & 0x80), 0);
    CHECK_EQ((int)(probe.b(531) & 1), 0);

    ObjLife9ResetHooks();
}
