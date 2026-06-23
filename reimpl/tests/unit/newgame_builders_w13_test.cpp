// =============================================================================
// WAVE-13 1:1 GOLDEN PINS — the new-game commit's command-packet builders and
// the person-create leaf, for the byte-exact field layouts the rest of the
// segment's tests use but never themselves asserted as builder wire images.
//
// Pinned here (values traced to the source's provenance comments + the
// VIBE_GameLogic / VIBE_Command decompile recovered in progress/newgame-apply.md):
//
//   * VIBE_Command_EnqueueObjectInteraction @0x4944f0 (opcode 11) — the two
//     parent creates the commit issues (0x533773.. / 0x5337a7..). Field layout
//     from src/sim/command_codec.cpp: kind byte +0x14, a2 +0x15, a4 +0x19,
//     a3(word) +0x1D, a5 +0x1F, a6 +0x23, a7 +0x24, a8 +0x25.
//   * VIBE_Command_QueueRequestCoord27 @0x494878 (opcode 27) — the six
//     reciprocal relation packets (0x533930..0x5339ae). Field layout: a1 +0x10,
//     a2 +0x14, a3(delta) +0x18, coordX +0x1C, coordY +0x24 (+0x20 left zero).
//   * VIBE_Person_CreateAndSpawn @0x58da70 (the default backend in
//     src/sim/person_create.cpp) — the observable record stamps the create
//     handlers + the start-gold scan key on: kind +2, id +4, alive +8 == 100,
//     gender +9, owner word +10, spawn-context bytes +356 / +357.
//
// EnqueueTradeRequest (opcode 12) is already pinned in command_apply7_test;
// EnqueueCmd15 (opcode 15) in sim_command_inherit_test; QueueRequestFlagBlob32
// (opcode 32) in command_apply7_test — those are NOT duplicated here.
// =============================================================================
#include "tests/framework/test.h"

#include "sim/command.h"
#include "sim/command_codec.h"   // EnqueueObjectInteraction / QueueRequestCoord27
#include "sim/person_create.h"
#include "sim/entity.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
CommandPacket& slot(CommandQueue& q, i32 idx) {
    return q.ring_slot(static_cast<u32>(idx));
}
} // namespace

// ===========================================================================
// opcode 11 — EnqueueObjectInteraction @0x4944f0 (the parent creates).
// The commit calls it twice as EnqueueObjectInteraction(9, -1, prof, -1, -1,
// ctx, 0, gender); we feed unique sentinel bytes per field to lock each slot.
// ===========================================================================
TEST(NewGameBuildersW13, ObjectInteractionOpcode11Layout) {
    CommandQueue q;
    // a1=kind (u8), a2 (i32), a3 (i16 word), a4 (i32), a5 (i32), a6/a7/a8 (u8).
    i32 idx = EnqueueObjectInteraction(q, 0x09, 0x11223344, 0x5566,
                                       0x778899AA, static_cast<i32>(0xBBCCDDEEu),
                                       0xA1, 0xA2, 0xA3);
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 11u);
    CHECK_EQ((int)p.bytes[0x14], 0x09);                 // a1 kind byte
    CHECK_EQ(p.get32(0x15), 0x11223344u);               // a2
    CHECK_EQ(p.get32(0x19), 0x778899AAu);               // a4
    CHECK_EQ((int)p.get16(0x1D), 0x5566);               // a3 (word)
    CHECK_EQ(p.get32(0x1F), 0xBBCCDDEEu);               // a5
    CHECK_EQ((int)p.bytes[0x23], 0xA1);                 // a6 (parent prof ctx)
    CHECK_EQ((int)p.bytes[0x24], 0xA2);                 // a7
    CHECK_EQ((int)p.bytes[0x25], 0xA3);                 // a8 (gender flag)
}

// The exact two calls the commit makes: mother (gender flag 1) then father
// (gender flag 0). Kind 9, a4/a5 = -1, a7 = 0 in both (0x533773 / 0x5337a7).
TEST(NewGameBuildersW13, ObjectInteractionParentCalls) {
    CommandQueue q;
    i32 im = EnqueueObjectInteraction(q, 9, -1, /*prof=*/34, -1, -1,
                                      /*ctxA=*/0, 0, /*gender=*/1);
    i32 ifa = EnqueueObjectInteraction(q, 9, -1, /*prof=*/35, -1, -1,
                                       /*ctxB=*/0, 0, /*gender=*/0);
    CommandPacket& mo = slot(q, im);
    CommandPacket& fa = slot(q, ifa);
    // mother
    CHECK_EQ((int)mo.bytes[0x14], 9);
    CHECK_EQ(mo.get32(0x15), 0xFFFFFFFFu);             // a2 = -1
    CHECK_EQ((int)mo.get16(0x1D), 34);                 // prof word
    CHECK_EQ((int)mo.bytes[0x24], 0);                  // a7 = 0
    CHECK_EQ((int)mo.bytes[0x25], 1);                  // gender flag 1 (mother)
    // father
    CHECK_EQ((int)fa.bytes[0x14], 9);
    CHECK_EQ((int)fa.get16(0x1D), 35);
    CHECK_EQ((int)fa.bytes[0x25], 0);                  // gender flag 0 (father)
}

// ===========================================================================
// opcode 27 — QueueRequestCoord27 @0x494878 (the six relation packets).
// Field +0x20 (flt_62EB94 stash) is left zero by the builder.
// ===========================================================================
TEST(NewGameBuildersW13, Coord27Opcode27Layout) {
    CommandQueue q;
    i32 idx = QueueRequestCoord27(q, 0x01020304, 0x05060708, 127,
                                  static_cast<i32>(0xAABBCCDDu),
                                  static_cast<i32>(0xEEFF0011u));
    CHECK(idx >= 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.opcode(), 27u);
    CHECK_EQ(p.get32(0x10), 0x01020304u);              // a1 (subject id)
    CHECK_EQ(p.get32(0x14), 0x05060708u);              // a2 (object id)
    CHECK_EQ((int)p.get32(0x18), 127);                 // a3 (delta == 127)
    CHECK_EQ(p.get32(0x1C), 0xAABBCCDDu);              // coordX
    CHECK_EQ(p.get32(0x20), 0u);                        // +0x20 left zero
    CHECK_EQ(p.get32(0x24), 0xEEFF0011u);              // coordY
}

// The new-game pairs are all delta 127 / mode 0 (the builder passes
// coordX = coordY = 0 there): the third arg lands at +0x18 == 127, and the
// +0x18 word read by the apply handler (mode at +0x1C) stays 0.
TEST(NewGameBuildersW13, Coord27NewGameDelta127Mode0) {
    CommandQueue q;
    i32 idx = QueueRequestCoord27(q, 4242, 7777, 127, 0, 0);
    CommandPacket& p = slot(q, idx);
    CHECK_EQ(p.get32(0x10), 4242u);
    CHECK_EQ(p.get32(0x14), 7777u);
    CHECK_EQ((int)p.get32(0x18), 127);                 // delta 127
    CHECK_EQ(p.get32(0x1C), 0u);                        // mode 0 (coordX)
}

// ===========================================================================
// VIBE_Person_CreateAndSpawn @0x58da70 — the default backend's observable
// record stamps (the create handlers ExCreatePersonA/B + the start-gold scan
// key on these exact offsets).
// ===========================================================================
namespace {
void ResetPersonWorld() {
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    ResetEntityArrays();
    ResetPersonCreate();
    g_personArrayLoaded = true;
}
} // namespace

TEST(NewGameBuildersW13, PersonCreateStampsRecordOffsets) {
    ResetPersonWorld();
    g_personNextId = 5000;

    PersonSpawnArgs a{};
    a.kind      = 6;            // -> rec[+2]
    a.ownerWord = 0x1234;      // -> rec word +10
    a.a6        = 0x55;        // -> rec[+356]
    a.a7        = 0x66;        // -> rec[+357]
    a.a8        = 1;           // gender -> rec[+9]
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);

    const u8* rec = reinterpret_cast<const u8*>(&g_persons[idx]);
    CHECK_EQ((int)rec[2], 6);                  // kind byte +2
    i32 id; std::memcpy(&id, rec + 4, 4);
    CHECK_EQ(id, 5000);                        // id +4 (from allocator)
    CHECK_EQ((int)rec[8], 100);               // alive byte +8 == 100 (0x58da70)
    CHECK_EQ((int)rec[9], 1);                 // gender +9
    u16 ow; std::memcpy(&ow, rec + 10, 2);
    CHECK_EQ((int)ow, 0x1234);                // owner word +10
    CHECK_EQ((int)rec[356], 0x55);           // a6 +356
    CHECK_EQ((int)rec[357], 0x66);           // a7 +357

    // The allocator advances and mirrors into the parallel id column.
    CHECK_EQ(g_personIds[idx], 5000);
    CHECK_EQ(g_personNextId, 5001);
}

// Every created person is alive (+8 == 100) — the invariant the start-gold scan
// (0x533f42) and the round scans depend on, regardless of kind.
TEST(NewGameBuildersW13, PersonCreateAlwaysAlive) {
    ResetPersonWorld();
    for (u8 kind : {2, 6, 7, 9}) {
        PersonSpawnArgs a{}; a.kind = kind;
        u16 idx = Person_CreateAndSpawn(a);
        CHECK(idx != 0xFFFF);
        CHECK_EQ((int)reinterpret_cast<const u8*>(&g_persons[idx])[8], 100);
    }
}
