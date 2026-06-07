// tests/e2e/play_input_command_e2e_test.cpp — E2E: a scripted click SEQUENCE
// (driven through shim::ScriptedPlatform) issues several unit orders through the
// REAL CommandQueue, and the live world changes DETERMINISTICALLY.
//
// Each scripted frame: read the platform mouse (ScriptedPlatform), map the cursor
// to a world-view click, resolve a REAL object via scene_pick, build+enqueue the
// order via sim::IssueOnObject + the REAL codec, and apply it (FlushSendQueue +
// ExecCommands -> the opcode-80 handler mutates the live g_objects records).
//
// Determinism is the oracle (no original-binary reference): the same seed + the
// same scripted click sequence must drive play::HashFullWorld() to a byte-identical
// digest across reruns, and the digest must MOVE from its pre-click baseline (the
// orders genuinely mutated the world).
#include "test.h"

#include "play/input_command.h"
#include "play/scene_pick.h"
#include "play/world_digest.h"     // HashFullWorld
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"
#include "shim_impl/scripted_platform.h"
#include "shim/IPlatform.h"

#include <cstdint>
#include <vector>

using namespace guild;

namespace {

// The scripted scenario: a handful of objects on the ground at known world
// positions, and a click script (cursor x,y + which order mode) targeting them.
struct ClickStep { int sx, sy; play::CursorMode mode; i32 tileX, tileZ; };

// Project: world(x,0,z) -> screen ((x)*2+0.875, (z)*2+0.875) with eye at origin.
play::CityViewCamera Cam() {
    float eye[3] = {0, 0, 0};
    return play::MakeCityViewCamera(eye, 2.0f, 640, 480);
}

// Seed N objects on a deterministic grid; return their pick records.
std::vector<play::ScenePickObject> SeedObjects(int n) {
    sim::ResetEntityArrays();
    sim::g_sceneArrayLoaded = true;
    std::vector<play::ScenePickObject> objs;
    for (int i = 0; i < n; ++i) {
        i32 id = 0x1000 + i;
        sim::g_objects[i].alive = 1;
        sim::g_objects[i].id    = id;
        float wx = static_cast<float>(20 + i * 30);
        float wz = static_cast<float>(15 + i * 10);
        objs.push_back({id, {wx, 0.0f, wz}});
    }
    return objs;
}

// Run the full scripted sequence against a fresh world + queue, returning the
// final HashFullWorld digest. Drives the platform mouse each step.
std::uint64_t RunScript(const std::vector<ClickStep>& script) {
    auto objs = SeedObjects(static_cast<int>(script.size()));

    shim::ScriptedPlatform plat;
    plat.quitAfterPumps(static_cast<int>(script.size()));

    sim::CommandQueue q; q.Init();
    play::InstallOrderApplyHandler(q);
    play::SetInputCommandApplyHooks(nullptr);   // inert-default apply -> mutate recs

    play::CityViewCamera cam = Cam();
    int step = 0;
    while (plat.pumpMessages()) {
        const ClickStep& s = script[step];
        // Script the platform cursor for this frame, then read it back (the latch
        // the real input path performs).
        plat.setMouse(s.sx, s.sy, /*left=*/true);
        shim::MouseState m{}; plat.getMouse(m);

        sim::CombatOrderContext ctx{};
        ctx.worldToTile = [&s](float, float, float, i32& ox, i32& oz) {
            ox = s.tileX; oz = s.tileZ; return true;
        };
        ctx.findOrAllocSlot = [](i32, i32) { return true; };

        sim::CombatOrderHandle h{}; h.op80Owner = 0x100 + step;

        play::IssueWorldClick(q, cam, static_cast<float>(m.x), static_cast<float>(m.y),
                              objs.data(), static_cast<int>(objs.size()),
                              /*radius=*/12.0f, s.mode, /*attackAllowed=*/false, h, ctx);
        ++step;
    }

    std::uint64_t digest = play::HashFullWorld();
    sim::ResetEntityArrays();
    return digest;
}

} // namespace

// A 3-click scripted sequence: each click lands a conquer order on a distinct real
// object, mutating its record. The pre-click baseline digest differs from the
// post-sequence digest (the world changed), and the post-sequence digest is
// identical across two independent reruns (deterministic).
TEST(PlayInputCommandE2E, ScriptedSequenceMutatesWorldDeterministically) {
    // Baseline: seed the same objects but issue NO clicks.
    SeedObjects(3);
    std::uint64_t baseline = play::HashFullWorld();
    sim::ResetEntityArrays();

    std::vector<ClickStep> script = {
        // object 0 @ world(20,0,15) -> screen (40.875, 30.875)
        {41,  31, play::CursorMode::kConquer, 0x11, 0x21},
        // object 1 @ world(50,0,25) -> screen (100.875, 50.875)
        {101, 51, play::CursorMode::kConquer, 0x12, 0x22},
        // object 2 @ world(80,0,35) -> screen (160.875, 70.875)
        {161, 71, play::CursorMode::kConquer, 0x13, 0x23},
    };

    std::uint64_t run1 = RunScript(script);
    std::uint64_t run2 = RunScript(script);

    // The orders mutated the world (digest moved off the baseline)...
    CHECK(run1 != baseline);
    // ...and the same script reproduces the same world byte-for-byte.
    CHECK_EQ(run1, run2);
}

// A DIFFERENT script (different order kinds / tiles) yields a DIFFERENT digest —
// the world state is sensitive to the actual orders issued, not constant.
TEST(PlayInputCommandE2E, DifferentScriptDivergesDeterministically) {
    std::vector<ClickStep> scriptA = {
        {41,  31, play::CursorMode::kConquer, 0x11, 0x21},
        {101, 51, play::CursorMode::kConquer, 0x12, 0x22},
    };
    // Same picks, but different destination tiles -> different applied mutation.
    std::vector<ClickStep> scriptB = {
        {41,  31, play::CursorMode::kConquer, 0x55, 0x66},
        {101, 51, play::CursorMode::kConquer, 0x77, 0x18},
    };

    std::uint64_t a1 = RunScript(scriptA);
    std::uint64_t a2 = RunScript(scriptA);
    std::uint64_t b1 = RunScript(scriptB);

    CHECK_EQ(a1, a2);        // A reproducible
    CHECK(a1 != b1);         // A and B diverge (orders carried different tiles)
}

// The queue genuinely advanced: a multi-click script enqueues + applies one
// opcode-80 command per click through the REAL CommandQueue.
TEST(PlayInputCommandE2E, EachClickDrivesOneRealCommand) {
    auto objs = SeedObjects(4);
    sim::CommandQueue q; q.Init();
    play::InstallOrderApplyHandler(q);
    play::SetInputCommandApplyHooks(nullptr);

    play::CityViewCamera cam = Cam();
    std::vector<ClickStep> script = {
        {41,  31, play::CursorMode::kConquer, 1, 2},
        {101, 51, play::CursorMode::kConquer, 3, 4},
        {161, 71, play::CursorMode::kConquer, 5, 6},
        {221, 91, play::CursorMode::kConquer, 7, 8},
    };

    int issued = 0;
    for (std::size_t i = 0; i < script.size(); ++i) {
        const ClickStep& s = script[i];
        sim::CombatOrderContext ctx{};
        ctx.worldToTile = [&s](float, float, float, i32& ox, i32& oz) {
            ox = s.tileX; oz = s.tileZ; return true;
        };
        ctx.findOrAllocSlot = [](i32, i32) { return true; };
        sim::CombatOrderHandle h{}; h.op80Owner = static_cast<i32>(0x200 + i);

        play::WorldOrder o = play::IssueWorldClick(
            q, cam, static_cast<float>(s.sx), static_cast<float>(s.sy),
            objs.data(), static_cast<int>(objs.size()), 12.0f, s.mode, false, h, ctx);
        if (o.enqueued) ++issued;
        // Each click resolved its own real object.
        CHECK_EQ(o.resolveKind, 1);
        CHECK_EQ((int)o.kind, (int)play::kKindConquer);
    }

    CHECK_EQ(issued, 4);
    CHECK_EQ(q.send_count(), 4u);          // four real packets rode the send ring
    CHECK(q.received_head() == nullptr);   // all drained + applied

    sim::ResetEntityArrays();
}
