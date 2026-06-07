#include "sim/handler_entry.h"
#include "sim/he.h"
#include "sim/command.h"
#include "sim/command_builders.h"
#include "sim/gametime.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// netglue stub (standalone — never called, the queue applies locally).
namespace guild::sim { namespace netglue {
void SendPacket(const CommandPacket&) {}
} }

// ---------------------------------------------------------------------------
// End-to-end: a He handler record drives a short NPC-style behaviour. We build a
// handler entry in the table, then run a step sequence that (a) stamps the record
// state and (b) emits real network commands via the reconstructed builders onto a
// real CommandQueue. We verify the handler state and the enqueued packet bytes
// against a hand-computed reference.
// ---------------------------------------------------------------------------

namespace {

// A tiny NPC step machine over a HandlerRecord, driven by its He fields (he.h).
// Phase 0: arm an appointment (+82) and queue an entity-29 request, recording its
//          handle into +132 (He_ReqHandle); advance to phase 1.
// Phase 1: emit a "named object" interaction (op 53) and a relation-mood delta
//          (op 93), then mark done (state = -1).
struct NpcContext {
    CommandQueue* q;
    GameTime clock;
    std::vector<i32> slots; // ring slots of every packet we emitted
};

void NpcStep(HandlerRecord* r, NpcContext& ctx) {
    HeRecord* h = reinterpret_cast<HeRecord*>(r);
    switch (He_State(h)) {
        case 0: {
            // stamp the clock into the appointment slot (+82) and advance one
            // calendar day. GameTimeAdvance's first arg is an hour-delta that
            // carries into days (wrap 24), so +24 hours == +1 day.
            He_ApptTime(h) = ctx.clock;
            GameTimeAdvance(&He_ApptTime(h), /*hours*/24, 0, 0);
            // queue the entity request; store handle into +132
            i32 handle = QueueRequestEntity29(*ctx.q, /*a1*/0, h);
            He_ReqHandle(h) = handle;
            ctx.slots.push_back(handle);
            He_State(h) = 1;
            break;
        }
        case 1: {
            // He_CityId (+12) holds the person-id mirror that Alloc stamped;
            // HrField16 (+16) holds the descriptor's cityId field.
            i32 personId = He_CityId(h);
            i32 cityId   = HrField16(reinterpret_cast<HandlerRecord*>(h));
            ctx.slots.push_back(
                QueueRequestNamedObject53(*ctx.q, personId, cityId, "OBJ",
                                          /*a4*/0x55, /*a5*/0, "Pest"));
            ctx.slots.push_back(
                RequestBuildOp93(*ctx.q, personId, /*kind*/1, /*drop*/0, /*amount*/4));
            He_State(h) = -1;  // done
            break;
        }
        default: break;
    }
}

u32 rd32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

} // namespace

TEST(SimHeE2E, HandlerDrivenNpcEmitsRealCommands) {
    // --- set up the handler table + a person record --------------------------
    HandlerTable tbl;
    tbl.Init();
    tbl.RegisterHandlerByType(13, [](HandlerRecord*) {}, [](HandlerRecord*) -> i32 { return 0; });

    static u8 person[16];
    std::memset(person, 0, sizeof(person));
    *reinterpret_cast<u16*>(person + 0) = 0x0042;       // marker word -> He +8
    *reinterpret_cast<i32*>(person + 4) = 0x000000C8;   // person id 200 -> He +12
    tbl.set_person_find([](i32 id) -> const void* { return id == 200 ? person : nullptr; });

    // descriptor: kind 13, personId 200, cityId 0x12C (300)
    HeRecord desc;
    std::memset(&desc, 0, sizeof(desc));
    {
        u8* b = reinterpret_cast<u8*>(&desc);
        b[4] = 13;
        *reinterpret_cast<i32*>(b + 8)  = 200;
        *reinterpret_cast<i32*>(b + 12) = 0x12C;
    }
    HandlerRecord* r = tbl.AllocHandlerEntry(&desc);
    CHECK(r != nullptr);
    CHECK_EQ(tbl.live_count(), 1);

    HeRecord* h = reinterpret_cast<HeRecord*>(r);
    // Alloc layout: +4 ordinal (He_Id), +8 person marker word (He_CityIndex),
    // +12 person-id mirror (He_CityId), +16 descriptor cityId field (HrField16).
    CHECK_EQ(He_Id(h), (i32)0);            // ordinal (first alloc)
    CHECK_EQ((int)He_CityIndex(h), 0x42);  // person marker word -> record +8
    CHECK_EQ(He_CityId(h), (i32)200);      // person id mirror -> record +12
    CHECK_EQ(HrField16(r), (i32)0x12C);    // descriptor cityId -> record +16
    CHECK_EQ(He_State(h), 0);              // fresh state

    // --- drive the NPC step machine onto a real CommandQueue -----------------
    CommandQueue q;
    q.Init();
    NpcContext ctx{ &q, GameTime{}, {} };
    std::memset(&ctx.clock, 0, sizeof(ctx.clock));
    ctx.clock.day = 10;
    ctx.clock.hour = 8;

    NpcStep(r, ctx);   // phase 0 -> arms appointment, emits op29
    NpcStep(r, ctx);   // phase 1 -> emits op53 + op93, done

    // --- verify handler state ------------------------------------------------
    CHECK_EQ(He_State(h), (i32)-1);                 // done
    // appointment is clock + 1 day.
    CHECK_EQ(He_ApptTime(h).day, (i32)11);
    CHECK_EQ((int)He_ApptTime(h).hour, 8);
    CHECK_EQ(He_ReqHandle(h), ctx.slots[0]);        // stored op29 handle

    // --- verify the enqueued packets against the hand-computed reference -----
    CHECK_EQ((int)ctx.slots.size(), 3);
    CHECK_EQ(q.send_count(), (u32)3);

    // op29: snapshot of the record. +0x10 reads record+4 (the ordinal == 0),
    // appt day @+0x22 == 11, a1 byte @+0x3E == 0. (The marker word @+0x20 reads
    // record+80, which the descriptor left 0.)
    CommandPacket& p29 = q.ring_slot((u32)ctx.slots[0]);
    CHECK_EQ(p29.opcode(), (u8)29);
    CHECK_EQ(rd32(p29.bytes + 0x10), (u32)0);
    CHECK_EQ(rd32(p29.bytes + 0x22), (u32)11);
    CHECK_EQ(p29.bytes[0x3E], (u8)0);

    // op53: named object interaction. a1 @+0x10 == person id 200, a2 @+0x14 ==
    // cityId 300, a4 @+0x18 == 0x55, obj string at +0x1D, name "Pest" at +0x3D.
    CommandPacket& p53 = q.ring_slot((u32)ctx.slots[1]);
    CHECK_EQ(p53.opcode(), (u8)53);
    CHECK_EQ(rd32(p53.bytes + 0x10), (u32)200);
    CHECK_EQ(rd32(p53.bytes + 0x14), (u32)300);
    CHECK_EQ(rd32(p53.bytes + 0x18), (u32)0x55);
    CHECK_EQ(std::memcmp(p53.bytes + 0x1D, "OBJ", 4), 0);
    CHECK_EQ(std::memcmp(p53.bytes + 0x3D, "Pest", 5), 0);

    // op93: relation-mood delta. a1 @+0x10 == person id 200, a2 @+0x14 == 1,
    // a4 @+0x18 == 4.
    CommandPacket& p93 = q.ring_slot((u32)ctx.slots[2]);
    CHECK_EQ(p93.opcode(), (u8)93);
    CHECK_EQ(rd32(p93.bytes + 0x10), (u32)200);
    CHECK_EQ(rd32(p93.bytes + 0x14), (u32)1);
    CHECK_EQ(rd32(p93.bytes + 0x18), (u32)4);

    // The three packets are linked on the pending-send list in order, with
    // sequential Counts 1,2,3.
    CHECK_EQ(p29.count(), (u32)1);
    CHECK_EQ(p53.count(), (u32)2);
    CHECK_EQ(p93.count(), (u32)3);

    // --- exec the queue locally and confirm the handlers see them in order ---
    static std::vector<u8>* g_seen = nullptr;
    static std::vector<u8> seen;
    seen.clear(); g_seen = &seen;
    CommandQueue rx;
    rx.Init();
    auto rec = [](CommandQueue&, CommandPacket& pkt, AckEntry*) {
        if (g_seen) g_seen->push_back(pkt.opcode());
    };
    for (u8 op = 0; op < (u8)kNumOpcodes; ++op) rx.set_handler(op, rec);
    rx.StoreReceivedPacket(p29);
    rx.StoreReceivedPacket(p53);
    rx.StoreReceivedPacket(p93);
    rx.ExecCommands();
    g_seen = nullptr;
    CHECK_EQ((int)seen.size(), 3);
    CHECK_EQ((int)seen[0], 29);
    CHECK_EQ((int)seen[1], 53);
    CHECK_EQ((int)seen[2], 93);

    // --- finally free the handler entry; the table empties ------------------
    tbl.FreeHandlerEntry(r);
    CHECK_EQ(tbl.live_count(), 0);
    CHECK_EQ((int)HrKind(r), 0);
}
