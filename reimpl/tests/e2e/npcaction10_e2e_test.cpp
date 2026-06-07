// End-to-end flow across the NpcAction10 batch: drive a master-exam handler from
// spawn through panel acceptance, and a fire-spread handler through its full ignite
// -> spread -> extinguish lifecycle, asserting the +112 state progression and the
// emitted leaf-call sequence cross the functions exactly as the dispatcher would.
#include "test.h"

#include "sim/npcaction10.h"
#include "sim/npcaction.h"
#include "sim/gametime.h"

#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {

struct ERec { i32 id = 0; u8 kind = 0; u8 rank = 0; bool hasChar = false; };

struct E2E {
    std::vector<std::string> trace;
    ERec* person = nullptr;
    void* win = reinterpret_cast<void*>(0x1);
    bool formMatch = false;
    int  formCode = -1;
    int  rating = 0;
    int  handlerMatch = 0;  // remaining sibling fire handlers
    bool freed = false;
};

E2E* g = nullptr;

i32  e_packet(i32) { return 1; }
i32  e_e29(int a, HeRecord*) { g->trace.push_back("e29:" + std::to_string(a)); return 1000 + a; }
i32  e_free(HeRecord*) { g->freed = true; g->trace.push_back("free"); return 0; }
int  e_count(int) { return 0; }
bool e_anyMatch(int, i32) { return g->handlerMatch-- > 0; }
void* e_find(i32) { return g->person; }
void* e_query(i32) { return g->person; }
i32  e_obj(void* r) { return r ? static_cast<ERec*>(r)->id : -1; }
u8   e_kind(void* r) { return r ? static_cast<ERec*>(r)->kind : 0; }
u8   e_rank(void* r) { return r ? static_cast<ERec*>(r)->rank : 0; }
void e_msg(i32 t, int id) { g->trace.push_back("msg:" + std::to_string(t) + "," + std::to_string(id)); }
void e_coord(i32 a, i32 b, int d) { g->trace.push_back("c27:" + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(d)); }
void e_sample(int, const char* n) { g->trace.push_back(std::string("snd:") + n); }
i32  e_pc(HeRecord*) { g->trace.push_back("panelCreate"); return 1; }
i32  e_pd(HeRecord*) { g->trace.push_back("panelDestroy"); return 0; }
void e_rich(int id) { g->trace.push_back("rich:" + std::to_string(id)); }
void* e_pw(HeRecord*) { return g->win; }
bool e_fm(void*) { return g->formMatch; }
int  e_fc(void*) { return g->formCode; }
int  e_rating(void*, int) { return g->rating; }
void e_slot28(int k, i32, i32, i32 amt) { g->trace.push_back("slot28:" + std::to_string(k) + "," + std::to_string(amt)); }

NpcAction10Hooks MakeE2EHooks() {
    NpcAction10Hooks h{};
    h.packetStatus = e_packet; h.queueEntity29 = e_e29; h.freeHandlerEntry = e_free;
    h.countHandlers = e_count; h.anyHandlerMatchesEntity = e_anyMatch;
    h.findPersonById = e_find; h.personQueryBegin = e_query; h.objId = e_obj;
    h.kind = e_kind; h.recRank = e_rank; h.sendMessage = e_msg; h.requestCoord27 = e_coord;
    h.playSample = e_sample; h.eventPanelCreate = e_pc; h.eventPanelDestroy = e_pd;
    h.renderRichString = e_rich; h.panelWindow = e_pw; h.formEventMatches = e_fm;
    h.formEventCode = e_fc; h.buildingRating = e_rating; h.queueSlotReset28 = e_slot28;
    return h;
}

struct HeBuf { alignas(8) unsigned char bytes[600]; HeBuf(){std::memset(bytes,0,sizeof(bytes));}
               HeRecord* rec(){return reinterpret_cast<HeRecord*>(bytes);} };
void Set(HeRecord* h, int off, i32 v){*reinterpret_cast<i32*>(reinterpret_cast<u8*>(h)+off)=v;}
i32  Get(HeRecord* h, int off){return *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h)+off);}

} // namespace

// Master-exam: spawn (state 0) -> panel poll accept (state 1) -> free.
TEST(NpcAction10_E2E, MasterExamSpawnThroughAccept) {
    E2E e; g = &e; auto hk = MakeE2EHooks(); SetNpcAction10Hooks(&hk);
    ERec examiner; examiner.id = 1; examiner.kind = 6; e.person = &examiner;

    HeBuf b; Set(b.rec(), 112, 0);
    // state 0: spawn the panel + 24h timer, advance to state 1.
    i32 r0 = NpcAction10_MasterExamState(b.rec());
    (void)r0;
    CHECK_EQ(Get(b.rec(), 112), 1);
    CHECK(!e.freed);

    // state 1: form accept (1210) -> applause + relation + destroy + free.
    e.formMatch = true; e.formCode = 1210;
    i32 r1 = NpcAction10_MasterExamState(b.rec());
    (void)r1;
    CHECK(e.freed);
    bool sawAccept = false, sawDestroy = false;
    for (auto& c : e.trace) { if (c == "msg:1,4983") sawAccept = true; if (c == "panelDestroy") sawDestroy = true; }
    CHECK(sawAccept); CHECK(sawDestroy);
    SetNpcAction10Hooks(nullptr);
}

// Fire spread: ignite (0->1) ... burn loop (2) ... wait sibling (3) ... extinguish.
TEST(NpcAction10_E2E, FireLifecycleIgniteToExtinguish) {
    E2E e; g = &e; auto hk = MakeE2EHooks(); SetNpcAction10Hooks(&hk);
    HeBuf b; Set(b.rec(), 112, 0);

    NpcAction10_FireSpreadStep(b.rec());           // state 0 -> 1 (ignite SFX)
    CHECK_EQ(Get(b.rec(), 112), 1);

    NpcAction10_FireSpreadStep(b.rec());           // state 1 -> 2 (neighbour scan)
    CHECK_EQ(Get(b.rec(), 112), 2);

    Set(b.rec(), 236, -1);                          // no spread target -> state 3
    NpcAction10_FireSpreadStep(b.rec());
    CHECK_EQ(Get(b.rec(), 112), 3);

    e.handlerMatch = 0;                             // no siblings -> extinguish + free
    NpcAction10_FireSpreadStep(b.rec());
    CHECK(e.freed);
    bool sawBegin = false, sawEnd = false;
    for (auto& c : e.trace) { if (c == "snd:Brand_Beginn") sawBegin = true; if (c == "snd:Brand_Ende") sawEnd = true; }
    CHECK(sawBegin); CHECK(sawEnd);
    SetNpcAction10Hooks(nullptr);
}

// Kidnap ransom flows into a slot-reset command with the rank-scaled amount.
TEST(NpcAction10_E2E, KidnapRansomEmitsSlotReset) {
    E2E e; g = &e; auto hk = MakeE2EHooks(); SetNpcAction10Hooks(&hk);
    ERec victim; victim.id = 3; victim.rank = 1; e.person = &victim;
    // wealth path needs computeTotalWealth; install a small backend inline.
    hk.computeTotalWealth = [](void*) -> i32 { return 50000; };
    hk.personFindActive = [](void* r) { return r; };
    SetNpcAction10Hooks(&hk);

    HeBuf b; Set(b.rec(), 112, 0);
    *reinterpret_cast<u8*>(reinterpret_cast<u8*>(b.rec()) + 120) = 2;
    NpcAction10_KidnapCarryStep(b.rec());
    // rank 1 factor 0.02 * min(50000,cap) = 1000.
    bool saw = false; for (auto& c : e.trace) if (c == "slot28:62,1000") saw = true;
    CHECK(saw);
    SetNpcAction10Hooks(nullptr);
}
