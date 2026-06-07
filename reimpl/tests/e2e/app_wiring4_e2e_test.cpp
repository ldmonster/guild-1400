// End-to-end: a full init -> frame-run -> command-stream -> shutdown lifecycle
// with ALL FOUR sim hook installers active (InstallRealSimHooks{,2,3,4}) and the
// fourth-wave app hooks (sound3dInitPool / optionsChatHotkeyPanels /
// quickJumpContact) exercised. Builds GameApp on RealSubsystems + headless shims,
// populates a small real world (a scene container holding an object stack, a live
// building record, the person array loaded), runs frames under a broad feature
// mask that includes the newly-wired per-frame steps (options/chat panels +
// quick-jump), then applies a command STREAM (object-stock move, building free,
// person spawn) with every real apply leaf installed and verifies the real
// cross-module effects (the node array, building table and person array actually
// mutated), then runs the 13-step shutdown cleanly with no crash.
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/real_hooks.h"
#include "sim/real_hooks2.h"
#include "sim/real_hooks3.h"
#include "sim/real_hooks4.h"
#include "sim/command.h"
#include "sim/command_apply2.h"
#include "sim/command_apply3.h"
#include "sim/entity.h"
#include "sim/object.h"
#include "sim/building_lifecycle.h"
#include "sim/person_create.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild;
using guild::app::RealSubsystems;

namespace {

int CountLivePersons() {
    int n = 0;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != -1) ++n;
    return n;
}

} // namespace

TEST(AppWiring4E2E, AllRealHooksLifecycleMutatesRealCrossModuleState) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    auto sockPair = shim::LoopbackSocket::makePair();
    config::IniFile ini;

    RealSubsystems sub(&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini);
    app::GameApp appObj(plat, gfx, audio, sub);

    // ---- install ALL FOUR waves of cross-module sim hooks ------------------
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::InstallRealSimHooks3();
    sim::InstallRealSimHooks4();

    // ---- init lifecycle ----------------------------------------------------
    // (InitEngineAndScriptCommands' worldLoadBuildingAndObjectData hook resets the
    // entity arrays, so the world is populated AFTER init, mirroring the spine's
    // load-then-populate order.)
    CHECK(appObj.CreateMainWindow(1));
    CHECK(appObj.InitSubsystemsAndMovieDll());
    CHECK(appObj.InitDisplayAndPaths(1));
    CHECK(appObj.InitEngineAndScriptCommands());

    // ---- a small POPULATED real world (after init reset) -------------------
    sim::ResetEntityArrays();
    sim::ObjectResetContainerHeads();
    sim::ObjectSetCommandHook(nullptr);
    sim::ResetBuildingPersons();
    sim::ResetPersonCreate();
    sim::g_personArrayLoaded = true;
    sim::g_sceneArrayLoaded = true;
    sim::g_sceneNodeCount = 0;

    // A scene-node container (id 7000) holding a real object stack (proto 50 x 10).
    const int container = 10;
    sim::g_sceneNodes[container].type = 100;
    sim::g_sceneNodes[container].id = 7000;
    sim::g_sceneNodes[container].entityPtr = -1; // empty child head
    sim::g_sceneNodes[container].childPtr = -1;
    sim::ObSetLocation(container, -1);
    sim::ObSetOwner(container, -1);
    sim::ObSetAmount(container, 0);
    sim::g_sceneNodeCount = container + 1;
    int stack = sim::GameObjectAddObjektToParent(7000, /*proto*/50, /*amount*/10);
    CHECK(stack >= 0);
    CHECK_EQ(sim::ObGetAmount(stack), 10);

    // A live building record at slot 5 + a real object record id 5 resolving to it.
    sim::g_objects[0].alive = 1;
    sim::g_objects[0].id = 5;
    sim::BuildingPersonRec* bld = sim::BuildingPersonAt(5);
    CHECK(bld != nullptr);
    bld->marker = 5;
    bld->kind = 1;       // a normal (not destroyed=15) building
    bld->activeFlag = 1;

    const int personsBefore = CountLivePersons();

    // Bring the spatial-voice pool up (init-block hook): real audio::Sound3dPool.
    sub.soundLibInit(8, 2, 22050);
    sub.sound3dInitPool(16);
    CHECK(sub.firedReal("sound3dInitPool"));
    CHECK_EQ(sub.sound3dCapacity(), 16);

    // ---- run frames under a broad mask incl. the new per-frame steps -------
    const std::uint32_t broad =
        app::mask::kWidgetMouse | app::mask::kHudMouse | app::mask::kGameObjects |
        app::mask::kTooltips | app::mask::kRenderWorld | app::mask::kScripts |
        app::mask::kNetworkCommand | app::mask::kDayCycleMusic |
        app::mask::kWeatherSky | app::mask::kHudSelection | app::mask::kHudLabels |
        app::mask::kInputCommandPoll | app::mask::kOptionsAndPanels |
        app::mask::kQuickJump;

    for (int i = 0; i < 20; ++i)
        appObj.RunFrameLoop(broad);

    CHECK_EQ(sub.frameCount(), 20);
    // The newly-wired per-frame steps reached real code without crashing.
    CHECK(sub.firedReal("optionsChatHotkeyPanels"));
    CHECK(sub.firedReal("quickJumpContact"));
    CHECK_EQ(sub.chatLineCount(), 1);    // real ChatConsole assembled one line
    CHECK(!sub.quickJumpResolved());     // no live contact id -> real resolver miss

    // ---- apply a COMMAND STREAM with ALL real apply leaves installed -------
    // (1) object-stock move: remove 4 of proto 50 from container 7000 (0x0F).
    {
        sim::CommandPacket pkt{};
        std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
        pkt.opcode() = sim::kOp2RemapObjectPair;
        pkt.put32(0x14, 7000u);                 // src container
        pkt.put32(0x10, static_cast<u32>(-1));  // no dst
        pkt.bytes[0x1C] = 50;                   // proto key
        i32 amount = 4; std::memcpy(pkt.bytes + 0x1D, &amount, 4);
        sim::AckEntry ack{};
        CHECK_EQ(sim::ApplyPacket2(pkt, &ack), 0);
    }
    // The REAL node array shrank: the stack is now 6.
    CHECK_EQ(sim::ObGetAmount(stack), 6);

    // (2) building free: free building id 5 (0x0E) -> real lifecycle marks it.
    {
        sim::CommandPacket pkt{};
        std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
        pkt.opcode() = sim::kOp2RemapValidateObject;
        pkt.put16(0x10, 5);
        sim::AckEntry ack{};
        CHECK_EQ(sim::ApplyPacket2(pkt, &ack), 0);
    }
    // The REAL building record was freed (kind 15 == destroyed).
    CHECK_EQ((int)bld->kind, 15);
    CHECK_EQ((int)bld->activeFlag, 0);

    // (3) person spawn: create a person of kind 3 (0x49) -> real g_persons record.
    {
        sim::CommandPacket pkt{};
        std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
        pkt.opcode() = sim::kOp3SpawnAndPlaceCharacter;
        pkt.bytes[0x14] = 3;                    // kind
        pkt.put32(0x10, static_cast<u32>(-1));  // no place-target
        pkt.put32(0x17, static_cast<u32>(-1));  // no parent -> resolve gate skipped
        sim::AckEntry ack{};
        CHECK_EQ(sim::ApplyPacket3(pkt, &ack), 0);
    }
    // Exactly one real person record was allocated by the stream.
    CHECK_EQ(CountLivePersons(), personsBefore + 1);

    // ---- 13-step shutdown (no crash) ---------------------------------------
    appObj.Shutdown();
    CHECK(sub.firedReal("tdGameShutdownSubsystems"));
    CHECK(sub.firedReal("tdGameStateFreeAllResources"));

    // ---- tidy shared module state for other suites -------------------------
    sim::ResetApply2State();
    sim::ResetApply3State();
    sim::ResetBuildingPersons();
    sim::ResetPersonCreate();
    sim::ResetEntityArrays();
}
