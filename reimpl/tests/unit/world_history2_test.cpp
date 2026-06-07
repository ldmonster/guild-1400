// Unit tests for world/world_history2 — the VIBE_He_* history-event engine tail.
#include "test.h"

#include "world/world_history2.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// Sized to the real person record (the class byte lives at +358); not rec[16].
struct PersonRec { u8 b[600]; };

} // namespace

// --- ValidatePunishmentType golden vectors (oracle: the recovered branch logic).
TEST(WorldHistory2Unit, ValidatePunishmentTypeNullRecord) {
    CHECK_EQ(He_ValidatePunishmentType(nullptr, 0), -1);
    CHECK_EQ(He_ValidatePunishmentType(nullptr, 5), -1);   // null beats type check
}

TEST(WorldHistory2Unit, ValidatePunishmentTypeRangeAndDefault) {
    PersonRec r{}; std::memset(&r, 0, sizeof(r));
    CHECK_EQ(He_ValidatePunishmentType(&r, 10), -3);       // type >= 10
    CHECK_EQ(He_ValidatePunishmentType(&r, 11), -3);
    CHECK_EQ(He_ValidatePunishmentType(&r, 0), 0);         // ordinary types: ok
    CHECK_EQ(He_ValidatePunishmentType(&r, 4), 0);
    CHECK_EQ(He_ValidatePunishmentType(&r, 9), 0);
}

TEST(WorldHistory2Unit, ValidatePunishmentTypeFive) {
    PersonRec r{}; std::memset(&r, 0, sizeof(r));
    CHECK_EQ(He_ValidatePunishmentType(&r, 5), -4);        // +358 == 0 -> not eligible
    r.b[358] = 1;
    CHECK_EQ(He_ValidatePunishmentType(&r, 5), 0);         // +358 set -> ok
}

TEST(WorldHistory2Unit, ValidatePunishmentTypeSeven) {
    PersonRec r{}; std::memset(&r, 0, sizeof(r));
    CHECK_EQ(He_ValidatePunishmentType(&r, 7), -4);        // +13 == 0 -> not eligible
    r.b[13] = 1;
    CHECK_EQ(He_ValidatePunishmentType(&r, 7), 0);         // +13 set -> ok
    // +358 must NOT affect type 7
    PersonRec r2{}; std::memset(&r2, 0, sizeof(r2)); r2.b[358] = 1;
    CHECK_EQ(He_ValidatePunishmentType(&r2, 7), -4);
}

// --- Diagnostic logging routes through the hook with the exact format string.
namespace {
char g_logBuf[512];
int  g_logCalls;
void CaptureLog(const char* s) { std::strncpy(g_logBuf, s, sizeof(g_logBuf) - 1); ++g_logCalls; }
} // namespace

TEST(WorldHistory2Unit, LogInvalidAllocTypeFormat) {
    g_logBuf[0] = 0; g_logCalls = 0;
    WorldHistory2Hooks h{}; h.logMessage = CaptureLog;
    SetWorldHistory2Hooks(&h);

    u8 rec[16] = {0}; rec[0] = 42;   // HE-type byte
    He_LogInvalidAllocType(rec, "Hans");
    CHECK_EQ(g_logCalls, 1);
    CHECK_EQ(std::strcmp(g_logBuf,
        "he_AllocNone(): Invalid HE-Type : 42, von Spieler Hans"), 0);

    SetWorldHistory2Hooks(nullptr);
}

TEST(WorldHistory2Unit, LogInvalidRunTypeFormat) {
    g_logBuf[0] = 0; g_logCalls = 0;
    WorldHistory2Hooks h{}; h.logMessage = CaptureLog;
    SetWorldHistory2Hooks(&h);

    u8 rec[16] = {0}; rec[0] = 7;
    He_LogInvalidRunType(rec, "Greta");
    CHECK_EQ(g_logCalls, 1);
    CHECK_EQ(std::strcmp(g_logBuf,
        "he_RunNone(): Invalid HE-Type : 7, von Spieler Greta"), 0);

    SetWorldHistory2Hooks(nullptr);
}

// --- ProcessAllPlayerNews sweeps exactly 768 slots, indices 0..767.
namespace {
int g_newsCalls;
int g_newsFirst, g_newsLast;
void CaptureNews(u16 slot) {
    if (g_newsCalls == 0) g_newsFirst = slot;
    g_newsLast = slot;
    ++g_newsCalls;
}
} // namespace

TEST(WorldHistory2Unit, ProcessAllPlayerNewsSweep) {
    g_newsCalls = 0; g_newsFirst = -1; g_newsLast = -1;
    WorldHistory2Hooks h{}; h.processPlayerNews = CaptureNews;
    SetWorldHistory2Hooks(&h);

    He_ProcessAllPlayerNews();
    CHECK_EQ(g_newsCalls, 768);
    CHECK_EQ(g_newsFirst, 0);
    CHECK_EQ(g_newsLast, 767);

    SetWorldHistory2Hooks(nullptr);
}

// --- NullHandler runs and returns (smoke).
TEST(WorldHistory2Unit, NullHandlerNoCrash) {
    He_NullHandler();
    CHECK(true);
}

// --- DestroyIconGfx: node-present vs node-absent branches.
namespace {
int g_detachCalls, g_detachArg;
int g_arrangeCalls, g_arrangeArg;
void CapDetach(int n) { ++g_detachCalls; g_detachArg = n; }
void CapArrange(int p) { ++g_arrangeCalls; g_arrangeArg = p; }
} // namespace

TEST(WorldHistory2Unit, DestroyIconGfxWithNode) {
    g_detachCalls = 0; g_arrangeCalls = 0;
    WorldHistory2Hooks h{};
    h.objectDetachAndRelease = CapDetach;
    h.arrangeIconsInCircle = CapArrange;
    // objectFindByHandle default returns 1 -> arrange fires.
    SetWorldHistory2Hooks(&h);

    HeIconSlot s{}; s.entityId = 100; s.parent = 5; s.mesh = 77; s.node = 99;
    He_DestroyIconGfx(&s);
    CHECK_EQ(s.entityId, -1);
    CHECK_EQ(s.parent, 0);
    CHECK_EQ(s.mesh, 0);
    CHECK_EQ(s.node, 0);
    CHECK_EQ(g_detachCalls, 1);
    CHECK_EQ(g_detachArg, 99);          // released the node handle
    CHECK_EQ(g_arrangeCalls, 1);
    CHECK_EQ(g_arrangeArg, 77);         // arrange takes the mesh handle (v4)

    SetWorldHistory2Hooks(nullptr);
}

TEST(WorldHistory2Unit, DestroyIconGfxWithoutNode) {
    g_detachCalls = 0; g_arrangeCalls = 0;
    WorldHistory2Hooks h{};
    h.objectDetachAndRelease = CapDetach;
    h.arrangeIconsInCircle = CapArrange;
    SetWorldHistory2Hooks(&h);

    HeIconSlot s{}; s.entityId = 100; s.parent = 5; s.mesh = 77; s.node = 0;
    He_DestroyIconGfx(&s);
    CHECK_EQ(s.entityId, -1);
    CHECK_EQ(s.parent, 0);
    CHECK_EQ(s.mesh, 77);               // mesh untouched on the no-node path
    CHECK_EQ(g_detachCalls, 0);         // nothing released
    CHECK_EQ(g_arrangeCalls, 0);

    SetWorldHistory2Hooks(nullptr);
}

// --- DestroyStaleIcons: destroys slots whose parent id drifted; keeps fresh ones.
namespace {
// parentEntityId hook: returns a "live id" looked up from a small table keyed by
// the parent handle. Lets a test simulate an entity whose id changed.
int g_liveId[8];
int LiveIdLookup(int parent) { return (parent >= 0 && parent < 8) ? g_liveId[parent] : parent; }
} // namespace

TEST(WorldHistory2Unit, DestroyStaleIconsDropsDrifted) {
    HeIconPoolReset();
    HeIconSlot* pool = HeIconPool();
    // slot 0: fresh (live id matches stored)        -> kept
    pool[0].parent = 1; pool[0].entityId = 50; pool[0].node = 0; pool[0].mesh = 0;
    // slot 1: drifted (live id != stored)            -> destroyed
    pool[1].parent = 2; pool[1].entityId = 60; pool[1].node = 11; pool[1].mesh = 0;
    // slot 2: free (parent 0)                        -> skipped
    g_liveId[1] = 50;   // parent handle 1 still has id 50
    g_liveId[2] = 999;  // parent handle 2 now reports a different id

    WorldHistory2Hooks h{};
    h.parentEntityId = LiveIdLookup;   // iconsEnabled default 1
    SetWorldHistory2Hooks(&h);

    He_DestroyStaleIcons();
    CHECK_EQ(pool[0].entityId, 50);    // kept
    CHECK_EQ(pool[0].parent, 1);
    CHECK_EQ(pool[1].entityId, -1);    // destroyed
    CHECK_EQ(pool[1].parent, 0);

    SetWorldHistory2Hooks(nullptr);
}

TEST(WorldHistory2Unit, DestroyStaleIconsDropsAllWhenDisabled) {
    HeIconPoolReset();
    HeIconSlot* pool = HeIconPool();
    pool[0].parent = 1; pool[0].entityId = 50; pool[0].node = 7;
    g_liveId[1] = 50;   // would otherwise be fresh

    WorldHistory2Hooks h{};
    h.parentEntityId = LiveIdLookup;
    h.iconsEnabled = []() { return 0; };   // icons globally off
    SetWorldHistory2Hooks(&h);

    He_DestroyStaleIcons();
    CHECK_EQ(pool[0].entityId, -1);        // destroyed despite matching id
    CHECK_EQ(pool[0].parent, 0);

    SetWorldHistory2Hooks(nullptr);
}

// --- DestroyIconsForEntity: matches the +8 (mesh) field against the arg.
TEST(WorldHistory2Unit, DestroyIconsForEntityMatchesField8) {
    HeIconPoolReset();
    HeIconSlot* pool = HeIconPool();
    pool[0].mesh = 314; pool[0].entityId = 1; pool[0].node = 0;
    pool[1].mesh = 271; pool[1].entityId = 2; pool[1].node = 0;
    pool[2].mesh = 314; pool[2].entityId = 3; pool[2].node = 0;

    WorldHistory2Hooks h{};
    SetWorldHistory2Hooks(&h);

    He_DestroyIconsForEntity(314);
    CHECK_EQ(pool[0].entityId, -1);    // matched
    CHECK_EQ(pool[1].entityId, 2);     // not matched
    CHECK_EQ(pool[2].entityId, -1);    // matched

    SetWorldHistory2Hooks(nullptr);
}

// --- CreateGfxInfo: free-slot allocation, parent bind, mesh-build dispatch.
namespace {
int g_meshCalls;
HeIconSlot* g_meshSlot;
int CapMesh(HeIconSlot* s, int, int) { ++g_meshCalls; g_meshSlot = s; s->node = 4242; return 0; }
} // namespace

TEST(WorldHistory2Unit, CreateGfxInfoAllocatesFirstFreeSlot) {
    HeIconPoolReset();
    g_meshCalls = 0; g_meshSlot = nullptr;
    HeIconSlot* pool = HeIconPool();
    pool[0].parent = 9;   // slot 0 occupied
    pool[1].parent = 8;   // slot 1 occupied -> slot 2 is first free

    WorldHistory2Hooks h{};
    h.createIconMesh = CapMesh;
    h.parentEntityId = [](int p) { return p + 1000; };  // entity id = parent+1000
    // objectFindByHandle / iconsEnabled defaults: live + enabled.
    SetWorldHistory2Hooks(&h);

    int ok = He_CreateGfxInfo(/*parentEntity*/55, /*gfxArg*/7, "Burg");
    CHECK_EQ(ok, 1);
    CHECK_EQ(pool[2].parent, 55);
    CHECK_EQ(pool[2].entityId, 1055);  // from parentEntityId hook
    CHECK_EQ(g_meshCalls, 1);
    if (g_meshSlot) CHECK_EQ(g_meshSlot, &pool[2]);
    CHECK_EQ(pool[2].node, 4242);      // mesh hook mutated the slot

    SetWorldHistory2Hooks(nullptr);
}

TEST(WorldHistory2Unit, CreateGfxInfoFailsWhenDisabledOrFull) {
    HeIconPoolReset();
    WorldHistory2Hooks h{};
    h.iconsEnabled = []() { return 0; };
    SetWorldHistory2Hooks(&h);
    CHECK_EQ(He_CreateGfxInfo(1, 0, "x"), 0);   // disabled

    // Full pool (all parents non-zero).
    HeIconPoolReset();
    HeIconSlot* pool = HeIconPool();
    for (int i = 0; i < kHeIconSlotCount; ++i) pool[i].parent = i + 1;
    WorldHistory2Hooks h2{};   // icons enabled by default
    SetWorldHistory2Hooks(&h2);
    CHECK_EQ(He_CreateGfxInfo(1, 0, "x"), 0);   // full

    SetWorldHistory2Hooks(nullptr);
}

TEST(WorldHistory2Unit, CreateGfxInfoInvalidParentLogs) {
    HeIconPoolReset();
    g_logBuf[0] = 0; g_logCalls = 0;
    WorldHistory2Hooks h{};
    h.logMessage = CaptureLog;
    h.objectFindByHandle = [](int) { return 0; };   // parent handle invalid
    SetWorldHistory2Hooks(&h);

    int ok = He_CreateGfxInfo(55, 0, "Ruine");
    CHECK_EQ(ok, 0);
    CHECK_EQ(g_logCalls, 1);
    CHECK_EQ(std::strcmp(g_logBuf, "he_CreateGfxInfo(): invalid parent :Ruine"), 0);

    SetWorldHistory2Hooks(nullptr);
}
