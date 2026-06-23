// Verifies InstallRealCharRenderWiring() binds the reconstructed Character action-queue
// pool allocator into CharRender4Hooks and the reconstructed VIBE_Util_StrCmp into
// CharRender5Hooks (both previously inert/abstracted at the process level), and that the
// real control flow runs over those bindings.
#include "tests/framework/test.h"

#include "sim/wire_charrender.h"
#include "sim/character_render4.h"  // CharRender4Hooks / GetCharRender4Hooks / builders
#include "sim/character_render5.h"  // CharRender5Hooks / GetCharRender5Hooks / SetCameraViewMode
#include "sim/charaction.h"         // RegisterHandlers / QueueInsertEntry / ActionNode
#include "sim/character.h"          // Character

namespace guild::render { int AnimStrCmp(const char* a, const char* b); }  // 0x5d3f10

using namespace guild;
using namespace guild::sim;

// The wiring installs PROCESS-LIFETIME tables. Bind once; the cases below read them.
TEST(WireCharRender, BindsRender4PoolAndRender5StrCmp) {
    // Baseline: a fresh CharRender4 default leaves the allocator inert (returns null).
    SetCharRender4Hooks(nullptr);
    CHECK(GetCharRender4Hooks().queueInsertEntry(nullptr) == nullptr);  // inert default

    InstallRealCharRenderWiring();

    // CharRender4: both leaves are now bound (non-null) — the genuine pool allocator
    // and unlinker. (The forwarders are file-local; we assert they are installed.)
    const CharRender4Hooks& h4 = GetCharRender4Hooks();
    CHECK(h4.queueInsertEntry != nullptr);
    CHECK(h4.unlinkEntry      != nullptr);

    // CharRender5: strCmp is bound to the reconstructed VIBE_Util_StrCmp (AnimStrCmp).
    // Equality result must match the real comparator exactly (0 == equal, case-sens).
    const CharRender5Hooks& h5 = GetCharRender5Hooks();
    CHECK(h5.strCmp != nullptr);
    CHECK_EQ(h5.strCmp("EGO", "EGO"), render::AnimStrCmp("EGO", "EGO"));
    CHECK(h5.strCmp("EGO", "EGO") == 0);
    CHECK(h5.strCmp("ego", "EGO") != 0);   // case-sensitive

    // The render-leaf fields stay on safe inert defaults (never null in the install).
    CHECK(h5.resolveMesh   != nullptr);
    CHECK(h5.terrainCodeAt != nullptr);
}

// CharRender4: with the real pool installed, a builder threads the REAL intrusive
// action queue (head at Character.actions) instead of early-outing on a null node.
TEST(WireCharRender, Render4BuilderThreadsRealQueue) {
    CHECK(RegisterHandlers() != 0);   // allocate the genuine node pool
    InstallRealCharRenderWiring();

    Character ch{};
    // CreateSoundActionEx grabs a node from the real pool, sets type 46, copies the name.
    ActionNode* node = CreateSoundActionEx(&ch, "ring_bell", 7, 0x3F800000 /*1.0f*/);
    CHECK(node != nullptr);                 // real allocator handed back a node
    CHECK(ch.actions != nullptr);           // it was threaded onto the real queue head
    CHECK_EQ(static_cast<int>(StepKindOf(node)), static_cast<int>(ActionStepKind::kSound));

    // Unlink it back through the real unlinker (the other bound leaf).
    GetCharRender4Hooks().unlinkEntry(node);
}

// CharRender5: the camera-view-mode dispatch resolves modes via the bound real StrCmp.
TEST(WireCharRender, Render5CameraDispatchViaRealStrCmp) {
    InstallRealCharRenderWiring();

    int actor = 1;
    // The installed setupAttachCamera leaf is inert, but the dispatch still returns 0
    // for a valid actor + recognized (case-sensitive) view name, and 1 only on a null
    // actor (which reports a script error).
    CHECK_EQ(SetCameraViewMode(actor, &actor, "CLOSEUP"), 0);
    CHECK_EQ(SetCameraViewMode(actor, &actor, "EGO"), 0);
    // lowercase must NOT match the real (case-sensitive) comparator: still a valid
    // actor, so it returns 0 but selects no mode — defined behavior, no crash.
    CHECK_EQ(SetCameraViewMode(actor, &actor, "ego"), 0);
    // Null actor -> the dispatch reports + returns 1 (uses the inert scriptError leaf).
    CHECK_EQ(SetCameraViewMode(0, nullptr, "EGO"), 1);
}
