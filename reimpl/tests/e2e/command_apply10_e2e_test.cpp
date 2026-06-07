#include "test.h"
#include "sim/command_apply10.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// E2E: drive a realistic command-acceptance pipeline end to end. A command
// targets a person (slot 16) and a tile (slot 20); the dispatcher runs the
// Check* gates in order, then — once accepted — a lockstep sync flushes a small
// range and CheckSyncRangeAcked polls the ACK table until the peer confirms it.

namespace {

struct E2E {
    void* resolvedObject = nullptr;
    void* resolvedImmediate = nullptr;
    bool  resolveOk = true;
    i32   count = 0;
    void* findRecord = nullptr;
};
E2E e;

int spyResolve(i32, void*, ResolvedEntity* out) {
    if (out) { *out = ResolvedEntity{}; out->object = e.resolvedObject; out->immediate = e.resolvedImmediate; }
    return e.resolveOk ? 1 : 0;
}
i32   spySpecial(i32) { return 0; }
void* spyFind(i32) { return e.findRecord; }
i32   spyCount(i32) { return e.count; }

void install() {
    ApplyTargetHooks h{};
    h.resolveEntityById    = &spyResolve;
    h.specialTarget        = &spySpecial;
    h.personFindRecordById = &spyFind;
    h.countAtLocation      = &spyCount;
    SetApplyTargetHooks(&h);
}

struct Cmd { u8 b[128]; Cmd() { std::memset(b, 0, sizeof b); } };
void p32(Cmd& c, int off, i32 v) { std::memcpy(c.b + off, &v, 4); }

} // namespace

TEST(CmdApply10_E2E, AcceptThenSyncConfirms) {
    install();

    // --- 1) Build a command whose target slots both pass the gates. ---------
    Cmd c;
    p32(c, 16, 40);   // "from" person id
    p32(c, 20, -1);   // "to" tile slot disabled (skips the reachable-to branch)
    p32(c, 29, 100);  // cooldown threshold high -> CountAtLocation below it

    // CheckTargetCooldown: slot 20 == -1 -> immediately reachable (returns 0).
    CHECK_EQ(CheckTargetCooldown(c.b, nullptr), 0);

    // CheckTargetNotInUse: the targeted person is found, but idle (state != 6/7)
    // so it is not bound elsewhere -> 0 (free to act).
    u8 person[600]; std::memset(person, 0, sizeof person);
    person[2] = 1;             // idle state
    e.findRecord = person;
    CHECK_EQ(CheckTargetNotInUse(c.b), 0);

    // CheckTargetOwnership: resolved object, but *(cmd+20) offset is 0 so the
    // alias slot (object+456) is not hit -> accepted (1).
    u8 obj[600]; std::memset(obj, 0, sizeof obj);
    e.resolvedObject = obj;
    p32(c, 20, 0);             // does not alias the reserved slot
    CHECK_EQ(CheckTargetOwnership(c.b, nullptr), 1);

    // --- 2) Lockstep sync: a 4-packet range [10,14). Initially pending. ------
    std::vector<u8> ack(0x8000 * 10, 0); // all status 0 (pending)
    CHECK_EQ(CheckSyncRangeAcked(10, 14, ack.data()), 0);

    // Peer ACKs all four -> fully acked.
    for (u32 s = 10; s < 14; ++s) ack[10 * (s & 0x7FFF)] = 1;
    CHECK_EQ(CheckSyncRangeAcked(10, 14, ack.data()), 1);

    // One of them comes back NAK -> negative ack signalled (-1).
    ack[10 * (12 & 0x7FFF)] = 2;
    CHECK_EQ(CheckSyncRangeAcked(10, 14, ack.data()), -1);
}

TEST(CmdApply10_E2E, RejectStopsPipeline) {
    install();

    // A command whose targeted person IS busy and bound to another object: the
    // not-in-use gate rejects, so the dispatcher would drop the command.
    Cmd c;
    p32(c, 16, 7);    // owner key
    p32(c, 20, 9);    // first probed person id

    u8 person[600]; std::memset(person, 0, sizeof person);
    person[2] = 6;                  // busy
    i32 bound = 999;                // bound to a different object than owner 7
    std::memcpy(person + 520, &bound, 4);
    e.findRecord = person;

    CHECK_EQ(CheckTargetNotInUse(c.b), 1);   // rejected: in use elsewhere

    // And when entity resolution fails entirely, the reachability gate also
    // reports unreachable (1), independently halting the pipeline.
    e.resolveOk = false;
    Cmd r;
    p32(r, 20, 5);                  // a real "to" slot to resolve
    CHECK_EQ(CheckTargetCooldown(r.b, nullptr), 1);
}
