// Golden-vector tests for the wave-20 command/turn/citystats/cutscene/crossfade
// leaves (gilde.exe). Each test pins the recovered byte arithmetic / control flow.
#include "sim/command_leaves.h"

#include "sim/actionqueue.h"
#include "sim/character.h"
#include "sim/charaction.h"
#include "sim/command.h"
#include "world/city.h"

#include "../framework/test.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// 0x485f7c — FindOrAllocSlot
// ---------------------------------------------------------------------------
TEST(CmdLeaves, FindOrAllocSlot_AllocsThenReuses) {
    SlotRoster roster;
    // Fresh roster: all keys -1.
    CHECK_EQ(roster.key(0), -1);

    // First key -> slot 0 (first free).
    int a = CommandFindOrAllocSlot(roster, 0x1111);
    CHECK_EQ(a, 0);
    CHECK_EQ(roster.key(0), 0x1111);
    CHECK_EQ(roster.slot_byte(0, kOrderSlotClaimedOff), (u8)0); // +0x98 cleared

    // Different key -> next free slot 1.
    int b = CommandFindOrAllocSlot(roster, 0x2222);
    CHECK_EQ(b, 1);
    CHECK_EQ(roster.key(1), 0x2222);

    // Re-requesting the first key returns the SAME slot (reuse pass wins).
    int a2 = CommandFindOrAllocSlot(roster, 0x1111);
    CHECK_EQ(a2, 0);
}

TEST(CmdLeaves, FindOrAllocSlot_FullRosterReturnsMinus1) {
    SlotRoster roster;
    for (int i = 0; i < kOrderSlotCount; ++i)
        CHECK_EQ(CommandFindOrAllocSlot(roster, 1000 + i), i);
    // 17th distinct key: roster full -> -1.
    CHECK_EQ(CommandFindOrAllocSlot(roster, 9999), -1);
    // But an existing key still resolves.
    CHECK_EQ(CommandFindOrAllocSlot(roster, 1005), 5);
}

TEST(CmdLeaves, FindOrAllocSlot_ZerosFreshSlot) {
    SlotRoster roster;
    // Dirty a slot body, then free it and re-alloc — alloc must zero 44 bytes.
    int s = CommandFindOrAllocSlot(roster, 0x55);
    roster.slot_byte(s, 10) = 0xAB;
    roster.put_key(s, -1);                 // free it
    int s2 = CommandFindOrAllocSlot(roster, 0x66);
    CHECK_EQ(s2, s);
    CHECK_EQ(roster.slot_byte(s2, 10), (u8)0);
    CHECK_EQ(roster.key(s2), 0x66);
}

// ---------------------------------------------------------------------------
// 0x49514c — QueueRequestGuardTarget61
// ---------------------------------------------------------------------------
TEST(CmdLeaves, GuardTarget61_NullOwnerReturnsMinus1) {
    CommandQueue q;
    q.Init();
    GuardTargetInputs in;
    in.hasOwner = false;
    CHECK_EQ(CommandQueueRequestGuardTarget61(q, in, 1, 2, 3), -1);
}

TEST(CmdLeaves, GuardTarget61_PacketBytes) {
    CommandQueue q;
    q.Init();
    GuardTargetInputs in;
    in.hasOwner = true;
    in.ownerId  = 0x44332211;
    in.hasSquad = true;
    in.squadId  = 0x77;
    in.squadFull = false;        // -> consult enemy
    in.hasEnemy = true;
    in.enemyId  = 0x0BAD;

    i32 ring = CommandQueueRequestGuardTarget61(q, in, /*mode*/0xAA, /*a3*/0xBB, /*a4*/0xCC);
    CHECK(ring >= 0);
    CommandPacket& p = q.ring_slot(static_cast<u32>(ring));
    CHECK_EQ(p.opcode(), (u8)61);                      // 0x3D
    CHECK_EQ(p.get32(0x10), (u32)0x44332211);          // owner id
    CHECK_EQ(p.get32(0x14), (u32)0x0BAD);              // enemy id
    CHECK_EQ(p.bytes[0x18], (u8)0xAA);                 // mode
    CHECK_EQ(p.get32(0x19), (u32)0x77);                // squad id
    CHECK_EQ(p.bytes[0x1D], (u8)0xCC);                 // a4
    CHECK_EQ(p.bytes[0x1E], (u8)0xBB);                 // a3
}

TEST(CmdLeaves, GuardTarget61_FullSquadSkipsEnemyScan) {
    // squad present AND full -> enemy id stays -1 (no nearest-enemy consult).
    CommandQueue q;
    q.Init();
    GuardTargetInputs in;
    in.hasOwner = true; in.ownerId = 5;
    in.hasSquad = true; in.squadId = 9; in.squadFull = true;
    in.hasEnemy = true; in.enemyId = 0x1234;   // would be used only if consulted
    i32 ring = CommandQueueRequestGuardTarget61(q, in, 0, 0, 0);
    CommandPacket& p = q.ring_slot(static_cast<u32>(ring));
    CHECK_EQ(p.get32(0x14), (u32)0xFFFFFFFF);  // enemy id = -1 (skipped)
}

// ---------------------------------------------------------------------------
// 0x5799a8 — GameTick_HandleTurnControlCommand
// ---------------------------------------------------------------------------
static CommandPacket MakeTagPacket(u32 tag) {
    CommandPacket p{};
    p.put32(36, tag);   // *((u32*)a1 + 9)
    return p;
}

TEST(CmdLeaves, TurnControl_EnableDisable) {
    TurnControlState st;
    CHECK_EQ(GameTickHandleTurnControlCommand(st, MakeTagPacket(kTagEnable)), 1);
    CHECK_EQ(st.latch(), 1);
    CHECK_EQ(GameTickHandleTurnControlCommand(st, MakeTagPacket(kTagDisable)), 1);
    CHECK_EQ(st.latch(), 0);
}

TEST(CmdLeaves, TurnControl_InitSetCopy36Bytes) {
    TurnControlState st;
    CommandPacket p = MakeTagPacket(kTagInit);
    // Distinct first 36 bytes (the copy source). +36 already holds the tag.
    for (int i = 0; i < 36; ++i) p.bytes[i] = static_cast<u8>(0x10 + i);
    CHECK_EQ(GameTickHandleTurnControlCommand(st, p), 1);
    for (int i = 0; i < 36; ++i)
        CHECK_EQ(st.block[i], p.bytes[i]);

    // 'set ' takes the same copy path.
    CommandPacket p2 = MakeTagPacket(kTagSet);
    p2.bytes[0] = 0xEE;
    CHECK_EQ(GameTickHandleTurnControlCommand(st, p2), 1);
    CHECK_EQ(st.block[0], (u8)0xEE);
}

TEST(CmdLeaves, TurnControl_UnknownTagReturns0) {
    TurnControlState st;
    st.set_latch(7);
    CHECK_EQ(GameTickHandleTurnControlCommand(st, MakeTagPacket(0xDEADBEEF)), 0);
    CHECK_EQ(st.latch(), 7);   // unchanged
}

// ---------------------------------------------------------------------------
// 0x5792e0 — City_ApplyStatsFromAck (fixed-point averaging, ConvertX truncation)
// ---------------------------------------------------------------------------
TEST(CmdLeaves, CityStats_AverageTruncates) {
    CityStatsAck ack;
    // cap divisor float at head[6].
    float cap = 12.5f;
    std::memcpy(&ack.head[6], &cap, sizeof(float));

    // Two active persons. Choose sat sums that average to a non-integer so the
    // truncation (vs round-to-nearest) is observable.
    // person A: sat = {3,3,3,3,3}, need=2 ; person B: sat = {2,2,2,2,2}, need=4
    CityStatsAck::Person a; a.needByte = 2; a.sat = {3,3,3,3,3};
    CityStatsAck::Person b; b.needByte = 4; b.sat = {2,2,2,2,2};
    ack.persons = {a, b};

    CityStatsResult r = CityApplyStatsFromAck(ack);
    CHECK_EQ(r.activeCount, 2);
    CHECK_EQ(r.capDivisor, 12.5f);
    CHECK_EQ(guild::world::g_capDivisor, 12.5f);   // write-through

    // sat[j] sum = 3+2 = 5; 5 * (1/2) = 2.5 -> trunc -> 2 for every j.
    for (int j = 0; j < 5; ++j)
        CHECK_EQ(r.averaged[j], (u8)2);

    // v4 = 5*(2+4) = 30; overall = 30 * (1/2) = 15.0 -> 15.
    CHECK_EQ(r.overall, (u8)15);
}

TEST(CmdLeaves, CityStats_TruncationNotRounding) {
    CityStatsAck ack;
    float cap = 1.0f; std::memcpy(&ack.head[6], &cap, sizeof(float));
    // 3 persons, sat sum = 8 -> 8/3 = 2.666.. -> trunc -> 2 (round would give 3).
    CityStatsAck::Person a; a.needByte = 0; a.sat = {3,3,3,3,3};
    CityStatsAck::Person b; b.needByte = 0; b.sat = {3,3,3,3,3};
    CityStatsAck::Person c; c.needByte = 0; c.sat = {2,2,2,2,2};
    ack.persons = {a, b, c};
    CityStatsResult r = CityApplyStatsFromAck(ack);
    CHECK_EQ(r.activeCount, 3);
    for (int j = 0; j < 5; ++j)
        CHECK_EQ(r.averaged[j], (u8)2);  // trunc(2.666) == 2
}

TEST(CmdLeaves, CityStats_EmptySetDefinedZero) {
    CityStatsAck ack;
    float cap = 3.0f; std::memcpy(&ack.head[6], &cap, sizeof(float));
    CityStatsResult r = CityApplyStatsFromAck(ack);   // no persons
    CHECK_EQ(r.activeCount, 0);
    CHECK_EQ(r.capDivisor, 3.0f);
    for (int j = 0; j < 5; ++j) CHECK_EQ(r.averaged[j], (u8)0);
    CHECK_EQ(r.overall, (u8)0);
}

// ---------------------------------------------------------------------------
// 0x4ac0c8 — Cutscene_CheckMaster
// ---------------------------------------------------------------------------
TEST(CmdLeaves, CheckMaster_MasterPresentReturns1) {
    CutsceneMasterInputs in;
    in.master = 5;
    in.masterIsParticipant = true;
    int broadcasts = 0;
    int rc = CutsceneCheckMaster(in, [&](i32){ ++broadcasts; });
    CHECK_EQ(rc, 1);
    CHECK_EQ(broadcasts, 0);
}

TEST(CmdLeaves, CheckMaster_NewMasterBroadcasts) {
    CutsceneMasterInputs in;
    in.master = 5;                  // current master id
    in.masterIsParticipant = false;
    in.masterRecValid = true;
    in.masterRecOwnerId = 5;        // current master record owner == master
    // A kind-6 participant owned by a DIFFERENT person -> master change.
    in.participants[0] = CutsceneMasterRecord{ /*id*/77, /*kind*/6, /*owner*/9, /*valid*/true };
    i32 broadcastTo = -1; int broadcasts = 0;
    int rc = CutsceneCheckMaster(in, [&](i32 owner){ broadcastTo = owner; ++broadcasts; });
    CHECK_EQ(rc, 0);
    CHECK_EQ(broadcasts, 1);
    CHECK_EQ(broadcastTo, 9);       // rec.ownerId of the new master
}

TEST(CmdLeaves, CheckMaster_StopsAtFirstKind67) {
    CutsceneMasterInputs in;
    in.master = 5;
    in.masterIsParticipant = false;
    in.masterRecValid = true; in.masterRecOwnerId = 5;
    // participant[0] is kind 7 (stops here, owner 11). participant[1] also kind 6
    // owner 22 — must NOT be selected because the loop stops at index 0.
    in.participants[0] = CutsceneMasterRecord{ 70, 7, 11, true };
    in.participants[1] = CutsceneMasterRecord{ 71, 6, 22, true };
    i32 broadcastTo = -1;
    int rc = CutsceneCheckMaster(in, [&](i32 owner){ broadcastTo = owner; });
    CHECK_EQ(rc, 0);
    CHECK_EQ(broadcastTo, 11);
}

TEST(CmdLeaves, CheckMaster_UnchangedReturns1) {
    CutsceneMasterInputs in;
    in.master = 9;
    in.masterIsParticipant = false;
    in.masterRecValid = true; in.masterRecOwnerId = 9;
    // A kind-6 participant whose owner equals the master -> no change.
    in.participants[0] = CutsceneMasterRecord{ 77, 6, 9, true };
    int broadcasts = 0;
    int rc = CutsceneCheckMaster(in, [&](i32){ ++broadcasts; });
    CHECK_EQ(rc, 1);
    CHECK_EQ(broadcasts, 0);
}

// ---------------------------------------------------------------------------
// 0x41e814 — Gfx_CrossFadeStep
// ---------------------------------------------------------------------------
TEST(CmdLeaves, CrossFade_RearmsWhileAlphaLow) {
    CrossFadeDesc fade;
    fade.active = true; fade.alpha = 100; fade.rows = 4; fade.width = 8; fade.srcStride = 16;
    int rearms = 0, blits = 0, teardowns = 0;
    CrossFadeOps ops;
    ops.setFadeParams = [&](i32 w, i32 s, i32 h, i32 a){
        ++rearms; CHECK_EQ(w, 8); CHECK_EQ(s, 16); CHECK_EQ(h, 4); CHECK_EQ(a, 108);
    };
    ops.blitRow = [&](i32){ ++blits; };
    ops.teardown = [&](){ ++teardowns; };
    bool alive = GfxCrossFadeStep(fade, ops);
    CHECK(alive);
    CHECK_EQ(fade.alpha, 108);    // +8
    CHECK_EQ(rearms, 1);
    CHECK_EQ(blits, 0);
    CHECK_EQ(teardowns, 0);
}

TEST(CmdLeaves, CrossFade_BlitsRowsInMidRange) {
    CrossFadeDesc fade;
    fade.active = true; fade.alpha = 280; fade.rows = 3;
    int rearms = 0, blits = 0, teardowns = 0;
    CrossFadeOps ops;
    ops.setFadeParams = [&](i32,i32,i32,i32){ ++rearms; };
    ops.blitRow = [&](i32){ ++blits; };
    ops.teardown = [&](){ ++teardowns; };
    bool alive = GfxCrossFadeStep(fade, ops);   // 280 -> 288 (<= 288, no teardown)
    CHECK(alive);
    CHECK_EQ(fade.alpha, 288);
    CHECK_EQ(rearms, 0);
    CHECK_EQ(blits, 3);           // one per row
    CHECK_EQ(teardowns, 0);
}

TEST(CmdLeaves, CrossFade_TearsDownPast288) {
    CrossFadeDesc fade;
    fade.active = true; fade.alpha = 285; fade.rows = 2;
    int teardowns = 0;
    CrossFadeOps ops;
    ops.blitRow = [&](i32){};
    ops.teardown = [&](){ ++teardowns; };
    bool alive = GfxCrossFadeStep(fade, ops);   // 285 -> 293 (> 288) -> teardown
    CHECK(!alive);
    CHECK_EQ(fade.alpha, 293);
    CHECK_EQ(teardowns, 1);
    CHECK(!fade.active);
}

TEST(CmdLeaves, CrossFade_InactiveNoOp) {
    CrossFadeDesc fade;   // active == false
    int calls = 0;
    CrossFadeOps ops;
    ops.setFadeParams = [&](i32,i32,i32,i32){ ++calls; };
    ops.blitRow = [&](i32){ ++calls; };
    ops.teardown = [&](){ ++calls; };
    CHECK(!GfxCrossFadeStep(fade, ops));
    CHECK_EQ(fade.alpha, 0);   // untouched
    CHECK_EQ(calls, 0);
}

// ---------------------------------------------------------------------------
// 0x40c2f0 — CharAction_InsertActionArgs
// ---------------------------------------------------------------------------
TEST(CmdLeaves, InsertActionArgs_NullCharFails) {
    CHECK(CharActionInsertActionArgs(nullptr, nullptr, 0, nullptr) == nullptr);
}

TEST(CmdLeaves, InsertActionArgs_TypeFromByte4AndReadyFromLowByte) {
    RegisterHandlers();                 // build the action registry + node pool
    // QueueInsertEntry only dereferences ch->actions (+296); a zeroed Character
    // is sufficient for the owner-storage path.
    static Character dummy{};
    dummy.actions = nullptr;
    Character* ch = &dummy;

    // type 0 (RunActionOrFree, registered with argCount 0). Pack: BYTE4 = 0,
    // low byte (ready) = 0x01.
    u64 packed = (static_cast<u64>(0) << 32) | 0x01u;
    i32 args[1] = {0};
    ActionNode* n = CharActionInsertActionArgs(ch, nullptr, packed, args);
    CHECK(n != nullptr);
    if (n) {
        CHECK_EQ(n->type, (u8)0);
        CHECK_EQ(n->ready, (u8)0x01);
        CHECK(n->owner == ch);
        CHECK(n->step == &RunActionOrFree);
        // RunActionOrFree latches type into args[1].
        CHECK_EQ(n->args[1], 0);
    }
    QueueShutdown();
}

TEST(CmdLeaves, InsertActionArgs_UnregisteredTypeClampsToZero) {
    RegisterHandlers();
    static Character dummy{};
    dummy.actions = nullptr;
    // type 40 is not in the built-in catalog (unregistered) -> clamps to 0.
    u64 packed = (static_cast<u64>(40) << 32) | 0x00u;
    ActionNode* n = CharActionInsertActionArgs(&dummy, nullptr, packed, nullptr);
    CHECK(n != nullptr);
    if (n) CHECK_EQ(n->type, (u8)0);
    QueueShutdown();
}
