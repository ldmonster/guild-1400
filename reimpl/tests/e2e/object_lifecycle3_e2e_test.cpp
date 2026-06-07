// End-to-end flow across src/sim/object_lifecycle3: spawn a node, place/scale it
// via the transform setters, toggle hidden state, find it by name, then kill it.
// Hooks capture the render/effect leaves so the node-field state machine is
// asserted faithfully.
#include "sim/object_lifecycle3.h"

#include <cstring>

#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {
int g_dirty = 0, g_shadow = 0, g_matrix = 0, g_detach = 0;
void eMark(SceneNode3*, u16, u8) { ++g_dirty; }
void eShadow(SceneNode3*, u16) { ++g_shadow; }
void eMatrix(const float*, SceneNode3*) { ++g_matrix; }
void eDetach(SceneNode3*) { ++g_detach; }
}  // namespace

TEST(ObjLife3E2E, TransformToggleFindKillFlow) {
    g_dirty = g_shadow = g_matrix = g_detach = 0;
    ObjLife3Hooks h;
    h.walkMarkDirty = eMark;
    h.traverseShadowReset = eShadow;
    h.matrixFromEuler = eMatrix;
    h.detachAndRelease = eDetach;
    ObjLife3SetHooks(h);
    g_objCurrent = nullptr;

    SceneNode3 node;
    std::strcpy(reinterpret_cast<char*>(node.raw), "room_hall");
    node.b(533) = 5;   // a "room" node (nodeType 5)

    // 1) place + scale the node.
    ObjectSetPositionXYZ(&node, 10.f, 20.f, 30.f);
    ObjectSetScaleVectorXYZ(&node, 2.f, 2.f, 2.f);
    CHECK_EQ(node.f(76 + 0), 10.f);
    CHECK_EQ(node.f(108 + 4), 2.f);
    CHECK((node.b(528) & 4) != 0);          // dirtied
    CHECK_EQ(g_dirty, 2);
    CHECK_EQ(g_shadow, 2);

    // 2) world translation builds a matrix.
    ObjectSetWorldTranslationXYZ(&node, 0.5f, 0.f, 0.f);
    CHECK_EQ(node.f(132), 0.5f);
    CHECK_EQ(g_matrix, 1);

    // 3) hide the room (nodeType 5 -> 6, requires name[0]=='r').
    node.b(0) = 'r';
    CHECK_EQ(ObjectToggleHiddenState(&node, 1, 0xABCD), 1);
    CHECK_EQ((int)node.b(533), 6);
    CHECK_EQ(node.d(64), (int)0xABCD);

    // 4) find it by name (case-insensitive).
    FindCtx ctx{};
    ctx.queryStr = "ROOM_HALL";
    bool keepWalking = ObjectMatchNameCallback(&node, &ctx);
    CHECK(ctx.found == &node);
    CHECK_EQ(keepWalking, false);

    // 5) un-hide then kill.
    CHECK_EQ(ObjectToggleHiddenState(&node, 0, 0), 1);
    CHECK_EQ((int)node.b(533), 5);
    CHECK_EQ(ObjectKillObject(&node), 1);
    CHECK_EQ(g_detach, 1);

    ObjLife3ResetHooks();
}
