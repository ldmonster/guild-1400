// e2e: a scripted "spawn + label + dirty + show" flow across object_lifecycle7's
// VIBE_Object_* leaves, with captor hooks standing in for the unreconstructed
// render/command leaves. Mirrors the sequence a script command would drive:
//   1. BuildModelName formats the model name and reparses it.
//   2. ApplyAnimScale sets the on-screen scale from the anim flags.
//   3. MarkDirtyFlag dirties the node; Reinitialize queues its redraw rect.
//   4. RequestChangeZustand queues a state-change command.
//   5. CmdShowObject attaches a spawned child and builds its light cache.
//   6. FormatNameWithCountRecursive renders the node tree's labels.
#include "test.h"

#include "sim/object_lifecycle7.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Captors shared across the flow.
int  g_parseResult = 0;
int  g_parseCalls = 0;
int  ParseStub(SceneNode7*) { g_parseCalls++; return g_parseResult; }

int  g_scale, g_baseW, g_baseH;
int  AnimStub(int, int* s, int* w, int* h) { *s = g_scale; *w = g_baseW; *h = g_baseH; return 1; }

int  g_attachRet, g_cacheCalls;
float g_attachX;
int  AttachStub(int, const float* p, void*, int) { g_attachX = p[0]; return g_attachRet; }
void CacheStub(int) { g_cacheCalls++; }

void* g_queryNode = nullptr;
int  g_appended = -1, g_queueRet = 0, g_beginCalls = 0;
void* QueryStub(int, int, int, int, int) { return g_queryNode; }
void BeginStub(void*, int) { g_beginCalls++; }
void AppendStub(unsigned, unsigned, const void* v, int) { g_appended = *static_cast<const char*>(v); }
int  QueueStub() { return g_queueRet; }

} // namespace

TEST(ObjectLifecycle7E2E, ScriptedSpawnAndShowFlow) {
    ObjLife7Hooks h{};
    h.parseNameAndBind             = ParseStub;
    h.animationFlagsCompute        = AnimStub;
    h.attachToUniverseNode         = AttachStub;
    h.lightBuildObjectCache        = CacheStub;
    h.gameObjectQueryFind          = QueryStub;
    h.commandBeginDeltaPacket      = BeginStub;
    h.commandAppendRawField        = AppendStub;
    h.commandQueueRequestState22   = QueueStub;
    ObjLife7SetHooks(h);

    // --- 1. Build the model name (mode 1 -> "ob_%s", model-kind 4). ---
    g_parseResult = 0xBEEF; g_parseCalls = 0;
    SceneNode7 node;
    int parsed = ObjectBuildModelName(&node, /*typeIdx*/ 0, /*flag90*/ 0,
                                      /*attachKind*/ 0, /*firstByteIsTen*/ 0,
                                      /*mode*/ 1, "Crate");
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(node.raw), "ob_Crate"), 0);
    CHECK_EQ((int)node.b(535), 4);
    CHECK_EQ(parsed, 0xBEEF);
    CHECK_EQ(g_parseCalls, 1);

    // --- 2. Apply the anim scale (scale 2 -> *2 on the +20/+22 words). ---
    g_scale = 2; g_baseW = 7; g_baseH = 9;
    ObjectApplyAnimScale(&node, /*handle*/ 3);
    CHECK_EQ(node.d(108), 2);
    CHECK_EQ((int)*reinterpret_cast<i16*>(node.raw + 20), 14);
    CHECK_EQ((int)*reinterpret_cast<i16*>(node.raw + 22), 18);

    // --- 3. Dirty the node, then queue its redraw rect. ---
    node.b(528) = 0; node.b(530) = 0xFF; node.b(531) = 0x01;
    ObjectMarkDirtyFlag(&node, /*clearHi*/ 1);
    CHECK_EQ((int)node.b(528), 0x04);
    CHECK_EQ((int)node.b(530), 0x7F);
    CHECK_EQ((int)node.b(531), 0x00);

    DirtyRectQueue q; q.base = 0x4000;
    int rectOff = ObjectReinitialize(&q, /*x*/ 8, /*y*/ 12, /*w*/ 64, /*h*/ 48,
                                     /*obj*/ 1, 0, 0, 1024, 768);
    CHECK_EQ(q.count, 1);
    CHECK_EQ(q.slots[0].x, 8);
    CHECK_EQ(q.slots[0].w, 64);
    CHECK_EQ(rectOff, 0x4000);

    // --- 4. Queue a state-change command (delta clamped against current). ---
    g_queryNode = reinterpret_cast<void*>(0x1234);
    g_appended = -1; g_queueRet = 55; g_beginCalls = 0;
    int qret = ObjectRequestChangeZustand(/*objId*/ 1, /*delta*/ 2, /*ctx*/ 0,
                                          /*curState*/ 5);
    CHECK_EQ(g_appended, 2);     // 5 + 2 >= 0 -> delta kept
    CHECK_EQ(g_beginCalls, 1);
    CHECK_EQ(qret, 55);

    // --- 5. Show a spawned child and build its light cache. ---
    g_attachRet = 0x99; g_cacheCalls = 0;
    float px = 3.0f, py = 4.0f, pz = 5.0f;
    int childHandle = ObjectCmdShowObject(&px, &py, &pz, nullptr);
    CHECK_EQ(childHandle, 0x99);
    CHECK_EQ(g_cacheCalls, 1);
    CHECK(g_attachX == 3.0f);

    ObjLife7ResetHooks();

    // --- 6. Render the (root -> child) label tree. ---
    const char* names[] = {"Crate", "Lid"};
    NameCountNode child; child.typeIndex = 1; child.count = 1;
    NameCountNode root;  root.typeIndex = 0; root.count = 3; root.firstChild = &child;
    char buf[600]; std::memset(buf, 0, sizeof(buf));
    char* deepest = ObjectFormatNameWithCountRecursive(&root, 0, buf, names);
    // The original indexes ONE shared _WORD[264] buffer by `&buf[depth]` (a 2-byte
    // step), so the child's write at buf+2 faithfully CLOBBERS the parent's tail:
    // the root "Crate (3)" at buf+0 becomes "Cr" + the child string. We assert the
    // surviving 2-byte prefix and the deepest (child) result the original returns.
    CHECK_EQ(std::strncmp(buf, "Cr", 2), 0);             // root's 2-byte prefix survives
    CHECK_EQ(std::strcmp(buf + 2, "Lid (1)"), 0);        // child wrote at buf+2
    CHECK(deepest != nullptr);
    if (deepest) CHECK_EQ(std::strcmp(deepest, "Lid (1)"), 0);  // deepest result
}
