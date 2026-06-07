// tests/integration/play_input_command_itest.cpp — INTEGRATION: a scripted
// world-view click resolves a REAL object, builds+enqueues a REAL Command packet
// via the genuine builders+codec, and applying it mutates the target's live record.
//
// Wires the REAL siblings exactly as the live click->order path does:
//   * play::scene_pick -> REAL sim::GameObjectResolveEntityById (entity.cpp) over a
//     seeded g_objects roster: the click resolves a genuine object (kind 1).
//   * sim::IssueOnObject (command_apply12.cpp) -> sim::BuildConquerCommand ->
//     RequestBuildOp80 -> REAL sim::CommandQueue::EnqueuePacket (command.cpp),
//     stamping the opcode-80 wire length via the REAL VIBE_Command_ComputePacketSize.
//   * FlushSendQueue + ExecCommands -> the installed opcode-80 handler -> the
//     default apply mutates the resolved ObjectRec.
#include "test.h"

#include "play/input_command.h"
#include "play/scene_pick.h"
#include "sim/command.h"
#include "sim/combat_packets.h"
#include "sim/entity.h"

#include <cstring>

using namespace guild;

namespace {
// A camera where world (x,0,z) projects to screen ((x-eye.x)*ppu+0.875,
// 0.875+(z-eye.z)*ppu). With eye at origin, ppu=2: world(50,0,30)->(100.875,60.875).
play::CityViewCamera Cam() {
    float eye[3] = {0, 0, 0};
    return play::MakeCityViewCamera(eye, 2.0f, 640, 480);
}

// A ctx whose heightmap projection returns a fixed tile (the conquer tile bytes).
sim::CombatOrderContext ProjCtx(i32 tx, i32 tz) {
    sim::CombatOrderContext c{};
    c.worldToTile = [tx, tz](float, float, float, i32& ox, i32& oz) {
        ox = tx; oz = tz; return true;
    };
    c.findOrAllocSlot = [](i32, i32) { return true; };
    return c;
}
} // namespace

// A WARE/conquer click on a REAL resolved object: scene_pick resolves the object
// (kind 1), IssueOnObject -> BuildConquerCommand enqueues an opcode-80 packet on
// the REAL queue, and ExecCommands' apply mutates the object record.
TEST(PlayInputCommandItest, ConquerOnRealObjectBuildsEnqueuesApplies) {
    sim::ResetEntityArrays();
    sim::g_sceneArrayLoaded = true;          // GameObjectResolveEntityById guard

    // Seed one live object the click will resolve.
    const i32 kObjId = 0x4242;
    sim::g_objects[7].alive = 1;
    sim::g_objects[7].id    = kObjId;

    sim::CommandQueue q; q.Init();
    play::InstallOrderApplyHandler(q);       // opcode-80 -> default apply
    play::SetInputCommandApplyHooks(nullptr); // use the inert-default apply

    // The object sits at world (50,0,30) -> screen (100.875, 60.875). Click there.
    play::ScenePickObject objs[1];
    objs[0] = {kObjId, {50, 0, 30}};

    sim::CombatOrderContext ctx = ProjCtx(/*tx=*/0x12, /*tz=*/0x34);
    sim::CombatOrderHandle h{}; h.op80Owner = 0xBEEF;

    play::WorldOrder o = play::IssueWorldClick(
        q, Cam(), 100.0f, 61.0f, objs, 1, /*radius=*/10.0f,
        play::CursorMode::kConquer, /*attackAllowed=*/false, h, ctx);

    // (1) The pick resolved the REAL object (kind 1 == object/building).
    CHECK_EQ(o.pickIndex, 0);
    CHECK_EQ((int)o.pickId, kObjId);
    CHECK_EQ(o.resolveKind, 1);

    // (2) The classifier picked the conquer kind, and a packet was enqueued.
    CHECK_EQ((int)o.kind, (int)play::kKindConquer);
    CHECK(o.issued);
    CHECK(o.enqueued);
    CHECK_EQ(q.send_count(), 1u);

    // (3) The REAL codec stamped an opcode-80 packet; the conquer staging carries
    //     kind 6 + the projected tile bytes; the wire length came from the REAL
    //     ComputePacketSize.
    sim::CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 80);
    CHECK_EQ((int)p.len(), (int)sim::ComputePacketSize(p));
    CHECK_EQ(p.get32(0x10), 0xBEEFu);                       // op80 owner @ +0x10
    const u8* stg = p.bytes + 0x14;                         // staging copied to +0x14
    CHECK_EQ((int)stg[sim::kSlotKind], (int)play::kKindConquer);   // kind 6
    CHECK_EQ((int)stg[sim::kConquerTileXByte], 0x12);
    CHECK_EQ((int)stg[sim::kConquerTileZByte], 0x34);
    CHECK_EQ(stg[0], (u8)(kObjId + 4));                     // *(target+4) low byte

    // (4) Applying it (FlushSendQueue+ExecCommands ran inside IssueWorldClick)
    //     mutated the resolved object's live record.
    sim::ObjectRec* rec = sim::BuildingFindById(kObjId);
    CHECK(rec != nullptr);
    if (rec) {
        const u8* b = reinterpret_cast<const u8*>(rec);
        CHECK_EQ((int)b[play::kAppliedKindOff], (int)play::kKindConquer);
        i32 dx = 0, dz = 0;
        std::memcpy(&dx, b + play::kAppliedDestXOff, 4);
        std::memcpy(&dz, b + play::kAppliedDestZOff, 4);
        CHECK_EQ(dx, 0x12);
        CHECK_EQ(dz, 0x34);
    }

    // The received list drained; the queue acked the applied packet.
    CHECK(q.received_head() == nullptr);
    CHECK_EQ(q.GetPacketStatusById(1), 2);                  // status 2 == applied

    sim::ResetEntityArrays();
}

// A click on EMPTY space (no object within the pick radius) takes the ground-move
// branch: kind 1 enqueued via the REAL codec, and a custom apply hook observes it.
TEST(PlayInputCommandItest, EmptySpaceGroundMoveAppliesViaHook) {
    sim::ResetEntityArrays();
    sim::g_sceneArrayLoaded = true;

    sim::CommandQueue q; q.Init();
    play::InstallOrderApplyHandler(q);

    // Capture the applied order through a custom apply hook (the live-wiring slot).
    struct Cap { i32 id; int kind; i32 dx; i32 dz; bool hit; } cap{0, 0, 0, 0, false};
    play::InputCommandApplyHooks ahk{};
    ahk.applyOrder = [&cap](i32 id, u8 kind, i32 dx, i32 dz) {
        cap = {id, (int)kind, dx, dz, true};
    };
    play::SetInputCommandApplyHooks(&ahk);

    // No objects under the cursor -> ground raycast (ctx supplies the tile).
    sim::CombatOrderContext ctx = ProjCtx(/*tx=*/0x07, /*tz=*/0x09);
    sim::CombatOrderHandle h{}; h.op80Owner = 0x1234;

    play::WorldOrder o = play::IssueWorldClick(
        q, Cam(), 5.0f, 5.0f, /*objects=*/nullptr, 0, /*radius=*/8.0f,
        play::CursorMode::kAttackMove, /*attackAllowed=*/false, h, ctx);

    CHECK_EQ(o.pickIndex, -1);                      // nothing under cursor
    CHECK_EQ((int)o.kind, (int)play::kKindGround);  // ground move (kind 1)
    CHECK(o.enqueued);

    sim::CommandPacket& p = q.ring_slot(1);
    CHECK_EQ((int)p.opcode(), 80);
    const u8* stg = p.bytes + 0x14;
    CHECK_EQ((int)stg[sim::kSlotKind], (int)play::kKindGround);  // kind 1
    // ground move stores the two raycast outs as dwords at staging +0x10/+0x14.
    CHECK_EQ(o.slot.get32(0x10), 0x07);
    CHECK_EQ(o.slot.get32(0x14), 0x09);

    // The custom apply hook saw the ground order (target id 0 == empty space).
    CHECK(cap.hit);
    CHECK_EQ(cap.kind, (int)play::kKindGround);
    CHECK_EQ(cap.dx, 0x07);
    CHECK_EQ(cap.dz, 0x09);

    play::SetInputCommandApplyHooks(nullptr);
    sim::ResetEntityArrays();
}
