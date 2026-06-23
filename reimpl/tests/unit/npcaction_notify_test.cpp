// Golden tests for gilde.exe 0x4c9dec VIBE_NpcAction_NotifyJoinLeaveGroup.
// A synthetic scene drives every FourCC branch; we assert the rendered message ids
// (in emission order), the recipients, and the command emissions 1:1 with the
// disassembly's control flow.
#include "sim/npcaction_notify.h"
#include "tests/framework/test.h"

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A synthetic person record: id (objId), kind, name word.
struct Person { i32 id; u8 kind; u16 name; };

// Global scene the hooks read.
struct Scene {
    std::vector<Person> persons;            // the 768-record city array (subset)
    i32  occupantFrom = 100;
    u16  occupantTeam = 0;
    int  seatSlots[4] = {-1, -1, -1, -1};
    // leader == persons[teamIndex]; here team 0 -> persons[0].
    // recordings:
    std::vector<int> rendered;
    std::vector<std::pair<i32, i32>> jumps; // (recipient, from)
    std::vector<std::tuple<i32, i32, i32>> money;   // (leader, member, amount)
    std::vector<std::tuple<i32, i32, int>> coord;   // (a, b, delta)
};
Scene* g = nullptr;

Person* find(i32 id) {
    if (!g) return nullptr;
    for (auto& p : g->persons) if (p.id == id) return &p;
    return nullptr;
}

void* h_find(i32 id)                  { return find(id); }
u16   h_team(void*)                   { return g->occupantTeam; }
i32   h_from(void*)                   { return g->occupantFrom; }
u16   h_occName(void*)                { return 7; }
u16   h_personName(void* r)           { return r ? static_cast<Person*>(r)->name : 0; }
i32   h_seatMember(void*, int slot)   { return g->seatSlots[slot]; }
u8    h_leaderKind(u16 team)          { return team < g->persons.size() ? g->persons[team].kind : 0; }
i32   h_leaderObj(u16 team)           { return team < g->persons.size() ? g->persons[team].id : 0; }
u8    h_memberKind(void* r)           { return r ? static_cast<Person*>(r)->kind : 0; }
i32   h_memberObj(void* r)            { return r ? static_cast<Person*>(r)->id : 0; }
u8    h_cityKind(int i)               { return i < (int)g->persons.size() ? g->persons[i].kind : 0; }
i32   h_cityObj(int i)                { return i < (int)g->persons.size() ? g->persons[i].id : 0; }
double h_price(i32, u8)               { return 12.0; } // 12+12 = 24 tab
void  h_jump(i32 r, i32 from, int, int){ g->jumps.push_back({r, from}); }
void  h_q16(i32 l, i32 m, i32 a, u8)  { g->money.push_back({l, m, a}); }
void  h_coord(i32 a, i32 b, int d)    { g->coord.push_back({a, b, d}); }
void  h_rendered(int t)               { g->rendered.push_back(t); }

NpcNotifyHooks MakeHooks() {
    NpcNotifyHooks h;
    h.findPersonById = h_find;
    h.occupantTeam = h_team;
    h.occupantFromId = h_from;
    h.occupantNameArg = h_occName;
    h.personNameArg = h_personName;
    h.seatMemberId = h_seatMember;
    h.leaderKind = h_leaderKind;
    h.leaderObjId = h_leaderObj;
    h.memberKind = h_memberKind;
    h.memberObjId = h_memberObj;
    h.cityPersonKind = h_cityKind;
    h.cityPersonObjId = h_cityObj;
    h.marketPrice = h_price;
    h.sendQuickjump = h_jump;
    h.queueRequest16 = h_q16;
    h.requestCoord27 = h_coord;
    h.onRendered = h_rendered;
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
TEST(NpcNotify, UnknownTagIsNoOp) {
    Scene s; g = &s;
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    void* occ = (void*)1;
    NpcActionNotifyJoinLeaveGroup(0xDEADBEEF, occ, (void*)2, 5);
    CHECK(s.rendered.empty());
    CHECK(s.jumps.empty());
    SetNpcNotifyHooks(nullptr);
}

// "cler": 5292 broadcast to player/host roster members only.
TEST(NpcNotify, ClearBroadcastsToPlayerMembers) {
    Scene s; g = &s;
    // leader = persons[0]; members in slots 1,2.
    s.persons = {{10, 6, 100}, {11, 6, 101}, {12, 3, 102}};
    s.seatSlots[0] = 11;  // player member -> messaged
    s.seatSlots[1] = 12;  // kind 3 -> NOT messaged
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagCler, (void*)1, (void*)2, -1);
    CHECK_EQ((int)s.rendered.size(), 1);
    CHECK_EQ(s.rendered[0], kNotifyTextClearBroad);          // 5292
    CHECK_EQ((int)s.jumps.size(), 1);
    CHECK_EQ(s.jumps[0].first, 11);                          // only the kind-6 member
    CHECK_EQ(s.jumps[0].second, s.occupantFrom);
    SetNpcNotifyHooks(nullptr);
}

// "new ": 5287 broadcast to every player/host city person except the leader.
TEST(NpcNotify, NewBroadcastsToCityPlayersExceptLeader) {
    Scene s; g = &s;
    s.persons = {{10, 6, 100}, {11, 6, 101}, {12, 7, 102}, {13, 3, 103}};
    s.occupantTeam = 0;   // leader = persons[0] = id 10
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagNew, (void*)1, (void*)2, -1);
    CHECK_EQ((int)s.rendered.size(), 1);
    CHECK_EQ(s.rendered[0], kNotifyTextNewBroad);            // 5287
    // ids 11 (kind6) and 12 (kind7) messaged; 10 is leader (skip), 13 kind3 (skip).
    CHECK_EQ((int)s.jumps.size(), 2);
    CHECK_EQ(s.jumps[0].first, 11);
    CHECK_EQ(s.jumps[1].first, 12);
    SetNpcNotifyHooks(nullptr);
}

// "join": person absent -> early-out (nothing rendered).
TEST(NpcNotify, JoinAbsentPersonEarlyOut) {
    Scene s; g = &s;
    s.persons = {{10, 6, 100}};
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagJoin, (void*)1, (void*)2, 999 /*absent*/);
    CHECK(s.rendered.empty());
    CHECK(s.jumps.empty());
    SetNpcNotifyHooks(nullptr);
}

// "join" with a present person + 3 members -> 5290 broadcast, member jumps, leader
// gets the 5283+5285 welcome concat.
TEST(NpcNotify, JoinThreeMembersWelcomeConcat) {
    Scene s; g = &s;
    // leader persons[0] id10 kind6; three roster members.
    s.persons = {{10, 6, 100}, {11, 6, 101}, {12, 6, 102}, {13, 6, 103}, {99, 6, 200}};
    s.seatSlots[0] = 11; s.seatSlots[1] = 12; s.seatSlots[2] = 13;  // 3 members
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagJoin, (void*)1, (void*)2, 99);
    // rendered: 5290 (broadcast), then 5283 + 5285 (leader welcome).
    CHECK_EQ((int)s.rendered.size(), 3);
    CHECK_EQ(s.rendered[0], kNotifyTextJoinBroad);           // 5290
    CHECK_EQ(s.rendered[1], kNotifyTextJoinMember);          // 5283
    CHECK_EQ(s.rendered[2], kNotifyTextJoin3rd);             // 5285
    // jumps: 3 members + 1 leader = 4.
    CHECK_EQ((int)s.jumps.size(), 4);
    CHECK_EQ(s.jumps[3].first, 10);                          // leader last
    SetNpcNotifyHooks(nullptr);
}

// "join" with 2 members (not 3) -> leader gets only 5283.
TEST(NpcNotify, JoinTwoMembersNoConcat) {
    Scene s; g = &s;
    s.persons = {{10, 6, 100}, {11, 6, 101}, {12, 6, 102}, {99, 6, 200}};
    s.seatSlots[0] = 11; s.seatSlots[1] = 12;  // 2 members
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagJoin, (void*)1, (void*)2, 99);
    CHECK_EQ((int)s.rendered.size(), 2);
    CHECK_EQ(s.rendered[0], kNotifyTextJoinBroad);           // 5290
    CHECK_EQ(s.rendered[1], kNotifyTextJoinMember);          // 5283 only
    SetNpcNotifyHooks(nullptr);
}

// "leav" with 1 member -> leader gets 5284+5286 farewell concat; the leaver is not
// re-messaged among members.
TEST(NpcNotify, LeaveSingleMemberFarewellConcat) {
    Scene s; g = &s;
    // leader persons[0] id10 kind6; one roster member (the leaver, id 11).
    s.persons = {{10, 6, 100}, {11, 6, 101}};
    s.seatSlots[0] = 11;
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagLeav, (void*)1, (void*)2, 11 /*the leaver*/);
    CHECK_EQ((int)s.rendered.size(), 3);
    CHECK_EQ(s.rendered[0], kNotifyTextLeaveBroad);          // 5291
    CHECK_EQ(s.rendered[1], kNotifyTextLeaveLeadr);          // 5284
    CHECK_EQ(s.rendered[2], kNotifyTextLeaveLast);           // 5286
    // members loop: the only member IS the leaver -> not messaged. leader messaged.
    CHECK_EQ((int)s.jumps.size(), 1);
    CHECK_EQ(s.jumps[0].first, 10);                          // leader only
    SetNpcNotifyHooks(nullptr);
}

// "exec": per-member tab transfer + reciprocal coord links + leader total (5282).
TEST(NpcNotify, ExecDistributesTabAndLinks) {
    Scene s; g = &s;
    // leader persons[0] id10 kind6; two roster members.
    s.persons = {{10, 6, 100}, {11, 6, 101}, {12, 6, 102}};
    s.seatSlots[0] = 11; s.seatSlots[1] = 12;
    NpcNotifyHooks h = MakeHooks();
    SetNpcNotifyHooks(&h);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagExec, (void*)1, (void*)2, -1);
    // tab per member = trunc(12+12) = 24; two members -> 2 money packets of 24.
    CHECK_EQ((int)s.money.size(), 2);
    CHECK_EQ(std::get<2>(s.money[0]), 24);
    CHECK_EQ(std::get<0>(s.money[0]), 10);                   // leader pays
    CHECK_EQ(std::get<1>(s.money[0]), 11);                   // first member
    // rendered: 5289 (tab), then 5282 (leader total).
    CHECK_EQ(s.rendered[0], kNotifyTextExecTab);             // 5289
    CHECK(std::find(s.rendered.begin(), s.rendered.end(),
                    kNotifyTextExecLeader) != s.rendered.end());
    // coord: 2 reciprocal leader<->member (2 each = 4) + 2 ordered member pairs
    // (i!=j) * 2 = (2*1)*2 = 4 -> total 8.
    CHECK_EQ((int)s.coord.size(), 8);
    SetNpcNotifyHooks(nullptr);
}

// Inert default: no hooks installed -> no crash, no effects.
TEST(NpcNotify, InertDefaultsAreSilent) {
    SetNpcNotifyHooks(nullptr);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagExec, nullptr, nullptr, -1);
    NpcActionNotifyJoinLeaveGroup(kNotifyTagNew, nullptr, nullptr, -1);
    CHECK(true);  // reached here without dereferencing null
}
