// Unit tests for the FOURTH wave of app-spine wiring (guild::app::RealSubsystems)
// plus the fourth-wave sim hook installer (guild::sim::InstallRealSimHooks4).
//
// Part (a) — app hooks moved STUB->REAL this pass forward to real reconstructed
// module entry points (observed via recorded events + module state):
//   sound3dInitPool        -> audio::Sound3dPool (84-byte spatial-voice pool)
//   optionsChatHotkeyPanels-> gui::ChatConsole (colour-prefix line assemble)
//   quickJumpContact       -> sim::GameObjectResolveEntityById (record probe)
//
// Part (b) — InstallRealSimHooks4 binds the command-APPLY leaves whose real
// reconstructed targets now exist so that applying a packet MUTATES the real
// shared entity arrays:
//   object stock move (apply2 0x0F)   -> sim::GameObjectRemoveObjektAmount
//                                        (removes a real stack from g_sceneNodes)
//   building free     (apply2 0x0E)   -> sim::Building_RemoveAndCleanup
//                                        (marks the real g_buildingPersons record)
//   person spawn      (apply3 0x49)   -> sim::Person_CreateAndSpawn
//                                        (allocates a real g_persons record)
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/real_hooks4.h"
#include "sim/command_apply2.h"
#include "sim/command_apply3.h"
#include "sim/command.h"
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
#include <memory>

using namespace guild;
using guild::app::RealSubsystems;
using namespace guild::sim;

namespace {

struct Fixture4 {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    std::pair<std::unique_ptr<shim::LoopbackSocket>, std::unique_ptr<shim::LoopbackSocket>> sockPair
        = shim::LoopbackSocket::makePair();
    config::IniFile ini;
    RealSubsystems sub{&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini};
};

// Fresh world for the object/entity-record paths.
void ResetWorld4() {
    ResetEntityArrays();
    ObjectResetContainerHeads();
    ObjectSetCommandHook(nullptr);
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;
    g_sceneNodeCount = 0;
    ResetBuildingPersons();
    ResetPersonCreate();
}

// Install a scene-node container at slot `i` (type!=0, id, empty child head).
int PutSceneContainer4(int i, i16 type, i32 id) {
    g_sceneNodes[i].type = type;
    g_sceneNodes[i].id = id;
    g_sceneNodes[i].entityPtr = -1;
    g_sceneNodes[i].childPtr = -1;
    ObSetLocation(i, -1);
    ObSetOwner(i, -1);
    ObSetAmount(i, 0);
    if (i + 1 > g_sceneNodeCount)
        g_sceneNodeCount = i + 1;
    return i;
}

} // namespace

// ===========================================================================
// Part (a): app hooks STUB -> REAL.
// ===========================================================================

TEST(AppWiring4, Sound3dInitPoolIsReal) {
    Fixture4 f;
    // The spatial pool is built over the SoundSystem voice pool; bring sound up.
    f.sub.soundLibInit(8, 2, 22050);
    f.sub.sound3dInitPool(12);
    CHECK(f.sub.firedReal("sound3dInitPool"));
    CHECK_EQ(f.sub.sound3dCapacity(), 12);
}

TEST(AppWiring4, OptionsChatHotkeyPanelsIsReal) {
    Fixture4 f;
    f.sub.optionsChatHotkeyPanels();
    CHECK(f.sub.firedReal("optionsChatHotkeyPanels"));
    // The real ChatConsole assembled + appended one line.
    CHECK_EQ(f.sub.chatLineCount(), 1);
}

TEST(AppWiring4, QuickJumpContactIsReal) {
    Fixture4 f;
    f.sub.quickJumpContact();
    CHECK(f.sub.firedReal("quickJumpContact"));
    // No live world -> the real resolver finds nothing (false), but ran.
    CHECK(!f.sub.quickJumpResolved());
}

// ===========================================================================
// Part (b): InstallRealSimHooks4 — applied commands mutate real records.
// ===========================================================================

// Before install, the apply2 object-stock leaf uses the inert default (returns
// success but does NOT touch g_sceneNodes). After install, it calls the real
// object module which removes the stack from the real node array.
TEST(AppWiring4, RemoveObjectCommandMutatesRealNodeArray) {
    ResetWorld4();
    ResetApply2State(); // start from the inert default backends

    // Real container + a real stock stack (proto 50, amount 10) in g_sceneNodes.
    int container = PutSceneContainer4(10, /*type*/100, /*id*/7000);
    int stack = GameObjectAddObjektToParent(7000, /*proto*/50, /*amount*/10);
    CHECK(stack >= 0);
    CHECK_EQ(ObGetAmount(stack), 10);
    int headBefore = g_sceneNodes[container].entityPtr;
    CHECK(headBefore >= 0);

    // Build a 0x0F (RemapObjectPair) packet: src container 7000 at +0x14, no dst
    // (+0x10 = -1), proto key at +0x1C, amount at +0x1D. The src leg calls
    // g_removeObjekt(resolved, proto, amount).
    CommandPacket pkt{};
    std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
    pkt.opcode() = kOp2RemapObjectPair;
    pkt.put32(0x14, 7000u);                  // src container id
    pkt.put32(0x10, static_cast<u32>(-1));   // dst: none
    pkt.bytes[0x1C] = 50;                    // proto key
    i32 amount = 4; std::memcpy(pkt.bytes + 0x1D, &amount, 4);

    // --- default backend: no real mutation -----------------------------------
    AckEntry ack0{};
    int r0 = ApplyPacket2(pkt, &ack0);
    CHECK_EQ(r0, 0);                          // default ObjStock returns success
    CHECK_EQ(ObGetAmount(stack), 10);         // but the real stock is UNCHANGED

    // --- after InstallRealSimHooks4: real removal ----------------------------
    InstallRealSimHooks4();
    AckEntry ack1{};
    int r1 = ApplyPacket2(pkt, &ack1);
    CHECK_EQ(r1, 0);
    CHECK_EQ(ObGetAmount(stack), 6);          // 10 - 4 removed from the REAL array

    // Drain the rest -> the real stack is removed from the node array.
    i32 rest = 6; std::memcpy(pkt.bytes + 0x1D, &rest, 4);
    AckEntry ack2{};
    ApplyPacket2(pkt, &ack2);
    CHECK_EQ(ObGetPrototype(stack), (i16)0);  // slot freed (prototype word cleared)
    CHECK_EQ(g_sceneNodes[container].entityPtr, -1); // child list now empty

    ResetApply2State(); // leave the module at its default for other suites
}

// A building-free command (apply2 0x0E) frees the real g_buildingPersons record
// via the building lifecycle (marks kind=15 destroyed) only after install.
TEST(AppWiring4, BuildingFreeCommandFreesRealBuildingRecord) {
    ResetWorld4();
    ResetApply2State();

    // Real object record id 5 -> resolves to the building-person slot 5.
    g_objects[0].alive = 1;
    g_objects[0].id = 5;
    // A live building at slot 5 (marker != -1 alive, kind not destroyed).
    BuildingPersonRec* b = BuildingPersonAt(5);
    CHECK(b != nullptr);
    b->marker = 5;        // alive (not the -1 free sentinel)
    b->kind = 1;          // a normal building (15 == destroyed)
    b->activeFlag = 1;

    // 0x0E (RemapAndValidateObject): id word at +0x10.
    CommandPacket pkt{};
    std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
    pkt.opcode() = kOp2RemapValidateObject;
    pkt.put16(0x10, 5);

    // --- default: returns success but the real building record is untouched ---
    AckEntry ack0{};
    int r0 = ApplyPacket2(pkt, &ack0);
    CHECK_EQ(r0, 0);
    CHECK_EQ((int)b->kind, 1); // still a live building under the default backend

    // --- after install: the real lifecycle frees the slot --------------------
    InstallRealSimHooks4();
    AckEntry ack1{};
    int r1 = ApplyPacket2(pkt, &ack1);
    CHECK_EQ(r1, 0);
    CHECK_EQ((int)b->kind, 15);   // REAL Building_RemoveAndCleanup marked it destroyed
    CHECK_EQ((int)b->activeFlag, 0);

    ResetApply2State();
    ResetBuildingPersons();
}

// A person-spawn command (apply3 0x49) allocates a real g_persons record only
// after install (the default returns a synthetic id without touching the array).
TEST(AppWiring4, SpawnCommandCreatesRealPersonRecord) {
    ResetWorld4();
    ResetApply3State();

    int liveBefore = 0;
    for (int i = 0; i < kPersonCapacity; ++i)
        if (g_persons[i].marker != -1) ++liveBefore;
    CHECK_EQ(liveBefore, 0);

    // 0x49 (SpawnAndPlaceCharacter): kind = byte +0x14. No parent (-1) so the
    // parent-resolve gate is skipped (the kind 0/255 path; use kind that does not
    // require a parent lookup). The handler reads kind at +0x14 and the two
    // spawn-context bytes at +0x1F / +0x20.
    CommandPacket pkt{};
    std::memset(pkt.bytes, 0, sizeof(pkt.bytes));
    pkt.opcode() = kOp3SpawnAndPlaceCharacter;
    pkt.bytes[0x14] = 3;                       // person kind
    pkt.put32(0x10, static_cast<u32>(-1));     // no place-target
    // +0x17 parent id field: -1 (no parent) so the parent-resolve branch is a no-op.
    pkt.put32(0x17, static_cast<u32>(-1));

    // --- default backend: synthetic id, NO real person created ----------------
    AckEntry ack0{};
    int r0 = ApplyPacket3(pkt, &ack0);
    CHECK_EQ(r0, 0);
    int liveAfterDefault = 0;
    for (int i = 0; i < kPersonCapacity; ++i)
        if (g_persons[i].marker != -1) ++liveAfterDefault;
    CHECK_EQ(liveAfterDefault, 0); // default did not allocate a real record

    // --- after install: a real person record is allocated --------------------
    InstallRealSimHooks4();
    AckEntry ack1{};
    int r1 = ApplyPacket3(pkt, &ack1);
    CHECK_EQ(r1, 0);
    int liveAfterReal = 0;
    for (int i = 0; i < kPersonCapacity; ++i)
        if (g_persons[i].marker != -1) ++liveAfterReal;
    CHECK_EQ(liveAfterReal, 1); // exactly one real person created in g_persons

    ResetApply3State();
    ResetPersonCreate();
}
