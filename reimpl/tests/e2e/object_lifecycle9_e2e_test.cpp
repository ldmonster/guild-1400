#include "test.h"

// End-to-end: drive a small object-lifecycle flow across the batch-9 functions
// using a shared captor-hook environment (a fake allocator + a fake universe).
// The flow: parse a node name -> clone it -> suspend/restore it -> move it
// between universes -> rebuild owned models -> teardown. Each step asserts the
// observable cross-function state the live game would carry.
#include "sim/object_lifecycle9.h"

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- one shared fake-heap + bookkeeping the whole flow uses ----
std::vector<void*> g_blocks;
int g_buildModelCalls = 0;
int g_inflateCalls = 0;

void* AllocHook(unsigned size, const char*) {
    void* p = std::calloc(1, size);
    g_blocks.push_back(p);
    return p;
}
void FreeHook(void*) { /* accounted by FreeAll at end */ }
void InitHook(SceneNode9*) {}
void SetPosHook(SceneNode9*, const float*) {}
void SetXlateHook(SceneNode9*, const float*) {}
void InflateHook(SceneNode9*) { ++g_inflateCalls; }
void BuildModelHook(SceneNode9*, void*, u8) { ++g_buildModelCalls; }
void WalkHook(void*, SceneNode9*, int, int) {}

ObjLife9Hooks MakeHooks() {
    ObjLife9Hooks h{};
    h.memAllocDebug = &AllocHook;
    h.memFreeDebug = &FreeHook;
    h.initStruct = &InitHook;
    h.setPosition = &SetPosHook;
    h.setWorldTranslation = &SetXlateHook;
    h.inflateGeometry = &InflateHook;
    h.buildModelNameByRow = &BuildModelHook;
    h.sceneWalkAndInvoke = &WalkHook;
    return h;
}
void FreeAll() { for (void* p : g_blocks) std::free(p); g_blocks.clear(); }

}  // namespace

TEST(ObjLifecycle9E2E, ParseCloneMoveRebuildTeardownFlow) {
    g_blocks.clear(); g_buildModelCalls = 0; g_inflateCalls = 0;
    ObjLife9Hooks h = MakeHooks();
    ObjLife9SetHooks(h);

    // --- type tables ---
    std::vector<u8> sceneType(kSceneTypeRows * kSceneTypeStride, 0);
    std::vector<u8> building(kBuildingRows * kBuildingStride, 0);
    std::strcpy(reinterpret_cast<char*>(&sceneType[12 * kSceneTypeStride + 1]), "wagon");

    // --- 1. build a source node, parse-and-bind its type word ---
    SceneNode9 src;
    src.b(n9::kAttachKind) = 7;          // a light-bearing node (kind>=5)
    src.f(n9::kPos + 0) = 5.f; src.f(n9::kPos + 4) = 6.f; src.f(n9::kPos + 8) = 7.f;
    src.d(n9::kOwnerTag) = 0x55AA;
    std::strcpy(reinterpret_cast<char*>(src.raw), "node_wagon");

    ParseTables9 pt;
    pt.sceneTypeBase = sceneType.data();
    pt.buildingBase  = building.data();
    pt.markerScene   = "node";           // prefix marker => scene branch
    pt.markerBuild   = "gb";
    i32 typeWord = 0;
    int parsed = ObjectParseNameAndBind("node_wagon", &typeWord, pt);
    CHECK_EQ(parsed, 1);
    CHECK_EQ(typeWord, (i32)(0x1000000 | 12));
    src.d(n9::kTypeWord) = typeWord;     // bind it onto the node

    // --- 2. clone the node (light block + node block from the shared heap) ---
    int allocsBefore = (int)g_blocks.size();
    void* clonePtr = ObjectClone(&src);
    CHECK(clonePtr != nullptr);
    // node + light block = 2 allocations.
    CHECK_EQ((int)g_blocks.size() - allocsBefore, 2);
    if (clonePtr) {
        SceneNode9* clone = static_cast<SceneNode9*>(clonePtr);
        CHECK(std::strcmp(reinterpret_cast<char*>(clone->raw), "node_wagon") == 0);
        CHECK_EQ(clone->f(n9::kPos + 8), 7.f);
        CHECK_EQ((int)(clone->b(n9::kFlags528) & 4), 4);   // dirty bit set by clone
        // The +512 owner tag lives inside the +488 392-byte run, which Clone only
        // copies when the SOURCE has a non-null +488 block (faithful gate). Our src
        // had no +488 block, so the owner tag is not carried — seed it explicitly
        // for the rebuild step below, exactly as the live spawn path would.
        clone->d(n9::kOwnerTag) = 0x55AA;

        // --- 3. suspend then restore the clone (attachKind 7 -> 1 -> 7) ---
        CHECK_EQ((int)ObjectToggleSuspendStateNamed(clone, /*hide=*/0), 1);
        CHECK_EQ((int)clone->b(n9::kAttachKind), 1);
        CHECK_EQ((int)clone->b(n9::kSavedKind), 7);
        CHECK_EQ((int)ObjectToggleSuspendStateNamed(clone, /*hide=*/1), 1);
        CHECK_EQ((int)clone->b(n9::kAttachKind), 7);

        // --- 4. move the clone between two universes ---
        Universe9 uniA, uniB;
        i32 cloneVal = (i32)reinterpret_cast<intptr_t>(clone);
        uniA.d(128) = cloneVal; uniA.d(132) = cloneVal;
        clone->d(n9::kFirstChild) = 0; clone->d(n9::kNextSibling) = 0;
        int inflateBefore = g_inflateCalls;
        char moved = ObjectMoveBetweenUniverses(clone, &uniA, &uniB, nullptr, nullptr);
        CHECK_EQ((int)moved, 1);
        CHECK_EQ(uniA.d(128), 0);                 // removed from A
        CHECK_EQ(uniB.d(132), cloneVal);          // appended to B
        CHECK_EQ(g_inflateCalls, inflateBefore + 1);

        // --- 5. rebuild every owned model row ---
        std::vector<u8> geb(kGebaeudeRows * kGebaeudeStride, 0);
        i32 owner = 0x55AA;
        geb[1 * kGebaeudeStride] = 1; std::memcpy(&geb[1 * kGebaeudeStride + 1], &owner, 4);
        geb[3 * kGebaeudeStride] = 1; std::memcpy(&geb[3 * kGebaeudeStride + 1], &owner, 4);
        g_buildModelCalls = 0;
        CHECK_EQ((int)ObjectRebuildModelByOwner(clone, geb.data()), 1);
        CHECK_EQ(g_buildModelCalls, 2);
    }

    FreeAll();
    ObjLife9ResetHooks();
}
