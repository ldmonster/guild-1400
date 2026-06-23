// Wave-8 W8-SMOKE — golden tests for the building chimney-smoke attach.
//   render::SpawnChimneySmoke         (0x4b60a0 VIBE_Object_SpawnChimneySmoke)
//   render::AttachCityBuildingSmoke   (0x504910 smoke arm RefreshBuildingEffects)
//
// The reconstruction reaches engine memory through BuildingFxHooks; the tests
// drive it over synthetic records / nodes / clock and assert the exact gate
// behaviour, the dummy world-position handoff, and the season time windows
// recovered by get_bytes (@0x6476FC / @0x64770C).
#include "render/building_fx.h"
#include "tests/framework/test.h"
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

using namespace guild;

namespace {

// ---------------------------------------------------------------------------
// Synthetic engine world. One "building record" carries the three fields the
// routine touches (type / building-node / smoke-script). The clock + effect
// slot + script VM are modeled as simple fixtures.
// ---------------------------------------------------------------------------
struct FakeRecord {
    u8    type        = 1;     // != 7 (so the type gate passes)
    void* buildingNode = (void*)0x1; // non-null (model node present)
    i32   smokeScript = -1;    // -1 == no smoke running
};

struct FakeWorld {
    // clock
    i32 day  = 0;     // season = day % 4
    i32 hour = 12;    // noon — inside every season window

    // type-table state byte for the record's type index (7 == "no smoke")
    u8  typeStateByte = 0;

    // effect-slot fixture. A matched slot row is a PREREQUISITE for spawning
    // (the building must be registered as a smoke-capable effect host); payload
    // 0 == not yet spawned. Defaults model a registered, not-yet-smoking host.
    bool  slotMatched = true;      // a row matched the record (ecx/v6 in the orig)
    void* slotPtr     = (void*)0x1;// the matched slot pointer (dword_12CEA8C[...])
    i32   slotPayload = 0;         // slot[+200] (non-zero == smoke already live)

    // dummy node: a frame whose float[19..21] is the dummy local origin and
    // float[30..32] is the local translation; float index 126 (byte 504) = parent
    // link (0 == no parent => PointThroughBoneChain returns translated origin).
    std::vector<float> dummyFrame;
    bool  dummyPresent = true;

    // script VM
    std::vector<i32> liveHandles;  // handles ScriptFindByHandle resolves
    int   runCalls = 0;
    int   finishCalls = 0;
    i32   lastWX = 0, lastWY = 0, lastWZ = 0;
    i32   nextHandle = 100;        // handle runSmokeScript returns
    bool  runFails = false;        // make the run return -1

    // slot-context observability
    int   switchCalls = 0;
    i32   activeSlot  = 7;         // "previous" slot the switch reports

    // city iterator
    std::vector<FakeRecord*> buildings;
    size_t iter = 0;

    FakeWorld() {
        dummyFrame.assign(200, 0.0f);
        // local translation (f[30..32]) — PointThroughBoneChain adds point + this.
        dummyFrame[30] = 0.0f; dummyFrame[31] = 0.0f; dummyFrame[32] = 0.0f;
        // the "point" we pass is node+19 (f[19..21]); set a recognizable origin.
        dummyFrame[19] = 100.0f; dummyFrame[20] = 250.0f; dummyFrame[21] = 400.0f;
        // parent link (byte 504 == f[126]) = 0 -> chain terminates immediately.
        dummyFrame[126] = 0.0f;
    }
};

FakeWorld* g_w = nullptr;

// --- hook trampolines onto g_w --------------------------------------------
u8 HType(void* r)                 { return ((FakeRecord*)r)->type; }
u8 HTypeState(u8 /*idx*/)         { return g_w->typeStateByte; }
void* HNode(void* r)             { return ((FakeRecord*)r)->buildingNode; }
i32 HGetScript(void* r)          { return ((FakeRecord*)r)->smokeScript; }
void HSetScript(void* r, i32 h)  { ((FakeRecord*)r)->smokeScript = h; }
i32 HDay()                        { return g_w->day; }
i32 HHour()                       { return g_w->hour; }
void* HFindSlot(void* /*r*/, bool* m) {
    if (m) *m = g_w->slotMatched;
    return g_w->slotPtr;
}
i32 HSlotPayload(void* /*s*/)     { return g_w->slotPayload; }
i32 HSwitch(i32 slot, const char*, i32) {
    g_w->switchCalls++;
    i32 prev = g_w->activeSlot;
    g_w->activeSlot = slot;
    return prev;
}
float* HFindDummy(void* /*node*/, const char*) {
    return g_w->dummyPresent ? g_w->dummyFrame.data() : nullptr;
}
i32 HRun(const char*, void*, i32 x, i32 y, i32 z) {
    g_w->runCalls++;
    g_w->lastWX = x; g_w->lastWY = y; g_w->lastWZ = z;
    if (g_w->runFails) return -1;
    i32 h = g_w->nextHandle++;
    g_w->liveHandles.push_back(h);
    return h;
}
void* HFindScript(i32 h) {
    for (i32 lh : g_w->liveHandles)
        if (lh == h) return (void*)(std::uintptr_t)h;
    return nullptr;
}
void HFinish(void* /*s*/) { g_w->finishCalls++; }
void* HBegin(void* /*world*/) {
    g_w->iter = 0;
    return g_w->buildings.empty() ? nullptr : (void*)g_w->buildings[0];
}
void* HNext() {
    if (g_w->iter + 1 >= g_w->buildings.size()) { g_w->iter = g_w->buildings.size(); return nullptr; }
    return (void*)g_w->buildings[++g_w->iter];
}
int g_flagCalls = 0;
void HFlag(void*) { g_flagCalls++; }

render::BuildingFxHooks MakeHooks() {
    render::BuildingFxHooks h;
    h.recordType            = HType;
    h.buildingTypeStateByte = HTypeState;
    h.recordBuildingNode    = HNode;
    h.recordSmokeScript     = HGetScript;
    h.setRecordSmokeScript  = HSetScript;
    h.gameTimeDay           = HDay;
    h.gameTimeHour          = HHour;
    h.findEffectSlot        = HFindSlot;
    h.effectSlotPayload     = HSlotPayload;
    h.switchActiveSlot      = HSwitch;
    h.findDummyNode         = HFindDummy;
    h.runSmokeScript        = HRun;
    h.findScriptByHandle    = HFindScript;
    h.finishScript          = HFinish;
    h.queryBuildingsBegin   = HBegin;
    h.queryBuildingsNext    = HNext;
    h.flagBuildingNode      = HFlag;
    return h;
}

struct HookGuard {
    render::BuildingFxHooks h;
    HookGuard(FakeWorld* w) { g_w = w; h = MakeHooks(); render::SetBuildingFxHooks(&h); }
    ~HookGuard() { render::SetBuildingFxHooks(nullptr); g_w = nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. The season smoke-window tables are bit-exact (@0x6476FC / @0x64770C).
// ---------------------------------------------------------------------------
TEST(BuildingFx, SeasonWindowTables) {
    CHECK_EQ(render::kSmokeStartHour[0], 8.0f);
    CHECK_EQ(render::kSmokeStartHour[1], 7.0f);
    CHECK_EQ(render::kSmokeStartHour[2], 8.0f);
    CHECK_EQ(render::kSmokeStartHour[3], 9.0f);
    CHECK_EQ(render::kSmokeEndHour[0], 20.0f);
    CHECK_EQ(render::kSmokeEndHour[1], 21.0f);
    CHECK_EQ(render::kSmokeEndHour[2], 20.0f);
    CHECK_EQ(render::kSmokeEndHour[3], 19.0f);
}

// ---------------------------------------------------------------------------
// 2. A building inside the window with no slot row and no running script SPAWNS
//    smoke at the dummy's world position and records the returned handle.
// ---------------------------------------------------------------------------
TEST(BuildingFx, SpawnsAtDummyWorldPosition) {
    FakeWorld w;
    w.day = 0; w.hour = 12;          // season 0, noon (8..20)
    HookGuard g(&w);
    FakeRecord rec;

    render::SpawnChimneySmoke(&rec, /*slotArg*/ 42);

    CHECK_EQ(w.runCalls, 1);
    // PointThroughBoneChain(node, node+19, world) with no parent ->
    //   world[k] = point[k] + translation[k] = dummyFrame[19+k] + dummyFrame[30+k]
    // args are pushed (z, y, x) == (world[2], world[1], world[0]).
    CHECK_EQ(w.lastWX, 100);   // world[0]
    CHECK_EQ(w.lastWY, 250);   // world[1]
    CHECK_EQ(w.lastWZ, 400);   // world[2]
    CHECK(rec.smokeScript != -1);      // handle stored
    CHECK_EQ(rec.smokeScript, 100);
    CHECK_EQ(w.switchCalls, 2);        // push slot 0 + restore prev
}

// ---------------------------------------------------------------------------
// 3. The type gate: a type-7 building never smokes (no run, no switch).
// ---------------------------------------------------------------------------
TEST(BuildingFx, Type7NoSmoke) {
    FakeWorld w;
    w.typeStateByte = 7;             // type-def state byte == 7
    HookGuard g(&w);
    FakeRecord rec;

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 0);
    CHECK_EQ(w.switchCalls, 0);
    CHECK_EQ(rec.smokeScript, -1);
}

// ---------------------------------------------------------------------------
// 4. No building model node -> early-out (no run).
// ---------------------------------------------------------------------------
TEST(BuildingFx, NoBuildingNodeNoSmoke) {
    FakeWorld w;
    HookGuard g(&w);
    FakeRecord rec;
    rec.buildingNode = nullptr;

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 0);
    CHECK_EQ(w.switchCalls, 0);
}

// ---------------------------------------------------------------------------
// 5. Time gate: before the window (hour 6 in season 0 = 8..20) -> no spawn.
//    After the window (hour 21) -> no spawn.
// ---------------------------------------------------------------------------
TEST(BuildingFx, OutsideWindowNoSmoke) {
    {
        FakeWorld w; w.day = 0; w.hour = 6;     // before 8:00
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 0);
        CHECK_EQ(rec.smokeScript, -1);
    }
    {
        FakeWorld w; w.day = 0; w.hour = 21;    // at/after 20:00 end
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 0);
    }
}

// ---------------------------------------------------------------------------
// 6. Season indexing: season 3 window is 9..19. Hour 8 is OUT; hour 9 is IN.
// ---------------------------------------------------------------------------
TEST(BuildingFx, Season3Window) {
    {
        FakeWorld w; w.day = 3; w.hour = 8;     // season 3, before 9:00
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 0);
    }
    {
        FakeWorld w; w.day = 3; w.hour = 9;     // season 3, 9:00 == start
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 1);
    }
    {
        FakeWorld w; w.day = 7; w.hour = 9;     // day 7 % 4 == 3 -> same window
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 1);
    }
}

// ---------------------------------------------------------------------------
// 7. Already-running script: record[+149] != -1 and a slot row matched ->
//    do nothing (no double-spawn).
// ---------------------------------------------------------------------------
TEST(BuildingFx, AlreadyRunningNoRespawn) {
    FakeWorld w; w.day = 0; w.hour = 12;
    w.slotMatched = true; w.slotPtr = (void*)0x1234; w.slotPayload = 0;
    HookGuard g(&w);
    FakeRecord rec;
    rec.smokeScript = 55;            // a handle is already running

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 0);
    CHECK_EQ(rec.smokeScript, 55);   // unchanged
}

// ---------------------------------------------------------------------------
// 8. Live effect slot (slot[+200] != 0) with a DEAD script handle: the expire
//    path clears record[+149] back to -1.
// ---------------------------------------------------------------------------
TEST(BuildingFx, ExpireDeadScript) {
    FakeWorld w;
    w.slotMatched = true; w.slotPtr = (void*)0x1; w.slotPayload = 1; // smoke up
    HookGuard g(&w);
    FakeRecord rec;
    rec.smokeScript = 77;            // handle recorded, but NOT in liveHandles
                                     // => ScriptFindByHandle returns null (dead)
    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(rec.smokeScript, -1);   // dropped
    CHECK_EQ(w.finishCalls, 0);      // nothing live to finish
    CHECK_EQ(w.runCalls, 0);
}

// ---------------------------------------------------------------------------
// 9. Live effect slot with a STILL-LIVE script: finish it and clear the handle.
// ---------------------------------------------------------------------------
TEST(BuildingFx, ExpireLiveScriptFinishes) {
    FakeWorld w;
    w.slotMatched = true; w.slotPtr = (void*)0x1; w.slotPayload = 1;
    w.liveHandles.push_back(88);     // handle 88 still resolves
    HookGuard g(&w);
    FakeRecord rec;
    rec.smokeScript = 88;

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.finishCalls, 1);
    CHECK_EQ(rec.smokeScript, -1);
}

// ---------------------------------------------------------------------------
// 10. Dummy node missing: switch happens (push/restore) but no run.
// ---------------------------------------------------------------------------
TEST(BuildingFx, MissingDummyNoRun) {
    FakeWorld w; w.day = 0; w.hour = 12;
    w.dummyPresent = false;
    HookGuard g(&w);
    FakeRecord rec;

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 0);
    CHECK_EQ(w.switchCalls, 2);      // still pushes + restores the slot
    CHECK_EQ(rec.smokeScript, -1);
}

// ---------------------------------------------------------------------------
// 11. Run fails (script missing / main rejected): handle NOT stored.
// ---------------------------------------------------------------------------
TEST(BuildingFx, RunFailKeepsHandleClear) {
    FakeWorld w; w.day = 0; w.hour = 12;
    w.runFails = true;
    HookGuard g(&w);
    FakeRecord rec;

    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 1);
    CHECK_EQ(rec.smokeScript, -1);   // not stored on failure
}

// ---------------------------------------------------------------------------
// 12. The CITY driver flags every building node and spawns per-building smoke.
// ---------------------------------------------------------------------------
TEST(BuildingFx, CityDriverAttachesAll) {
    FakeWorld w; w.day = 0; w.hour = 12;
    FakeRecord a, b, c;
    void* node = (void*)0x1;
    a.buildingNode = node; b.buildingNode = node; c.buildingNode = node;
    w.buildings = {&a, &b, &c};
    g_flagCalls = 0;
    HookGuard g(&w);

    render::BuildingSmokeAttachResult R = render::AttachCityBuildingSmoke((void*)0x9, 0);
    CHECK_EQ(R.buildings, 3);
    CHECK_EQ(R.smokeSpawned, 3);
    CHECK_EQ(g_flagCalls, 3);        // node[+530] flagged for each
    CHECK(a.smokeScript != -1);
    CHECK(b.smokeScript != -1);
    CHECK(c.smokeScript != -1);
}

// ---------------------------------------------------------------------------
// 13. The city driver skips a building with no model node (no flag, no spawn).
// ---------------------------------------------------------------------------
TEST(BuildingFx, CityDriverSkipsNodeless) {
    FakeWorld w; w.day = 0; w.hour = 12;
    FakeRecord a, b;
    a.buildingNode = (void*)0x1;
    b.buildingNode = nullptr;        // skipped
    w.buildings = {&a, &b};
    g_flagCalls = 0;
    HookGuard g(&w);

    render::BuildingSmokeAttachResult R = render::AttachCityBuildingSmoke((void*)0x9, 0);
    CHECK_EQ(R.buildings, 1);        // only the noded one visited SpawnChimneySmoke
    CHECK_EQ(R.smokeSpawned, 1);
    CHECK_EQ(g_flagCalls, 1);
    CHECK(a.smokeScript != -1);
    CHECK_EQ(b.smokeScript, -1);
}

// ---------------------------------------------------------------------------
// 14. The inert default hooks never crash and produce no smoke (null records /
//     empty world) — the headless build's safe path.
// ---------------------------------------------------------------------------
TEST(BuildingFx, InertDefaultsSafe) {
    render::SetBuildingFxHooks(nullptr);  // restore defaults
    render::SpawnChimneySmoke(nullptr, 0);          // null record -> no-op
    render::BuildingSmokeAttachResult R = render::AttachCityBuildingSmoke(nullptr, 0);
    CHECK_EQ(R.buildings, 0);
    CHECK_EQ(R.smokeSpawned, 0);
}

// ---------------------------------------------------------------------------
// 15. W10-SIM hardening: no matched effect slot -> spawn path is unreachable
//     (matched=false routes through LABEL_23 expire-check, no run, no switch).
// ---------------------------------------------------------------------------
TEST(BuildingFx, NoEffectSlotNoSpawn) {
    FakeWorld w; w.day = 0; w.hour = 12;  // inside the window
    w.slotMatched = false;                // no slot row matched the record
    w.slotPtr = nullptr;
    HookGuard g(&w);
    FakeRecord rec;
    render::SpawnChimneySmoke(&rec, 0);
    CHECK_EQ(w.runCalls, 0);              // no smoke spawned without a slot row
    CHECK_EQ(w.switchCalls, 0);
    CHECK_EQ(rec.smokeScript, -1);
}

// ---------------------------------------------------------------------------
// 16. W10-SIM hardening: a DEGENERATE negative `day` must not index the 4-entry
//     season tables out of bounds. day=-1 -> (-1 % 4) == -1; the `& 3` clamp
//     keeps the season index in [0,3] (== 3 here, the winter 9..19 window).
//     Without the guard this read kSmokeStartHour[255] (ASAN global-buffer-OOB).
// ---------------------------------------------------------------------------
TEST(BuildingFx, NegativeDaySeasonNoOOB) {
    {
        // (-1 & 3) == 3 -> season-3 window 9..19; hour 12 is INSIDE -> spawns.
        FakeWorld w; w.day = -1; w.hour = 12;
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 1);
    }
    {
        // hour 8 is OUTSIDE the season-3 window (9..19) -> no spawn, still no OOB.
        FakeWorld w; w.day = -5; w.hour = 8;   // (-5 & 3) == 3
        HookGuard g(&w);
        FakeRecord rec;
        render::SpawnChimneySmoke(&rec, 0);
        CHECK_EQ(w.runCalls, 0);
    }
}
