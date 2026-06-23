// gilde.exe 0x4b60a0 VIBE_Object_SpawnChimneySmoke + 0x504910 (smoke arm)
//   VIBE_Scene_RefreshBuildingEffects — the building chimney-smoke attach.
// See building_fx.h for the full data-flow recovery and address map.
#include "render/building_fx.h"
#include "render/particle_emitter_create.h"  // W8-EMITTER: SpawnEmitterAtPosition
#include "util/transform.h"                   // 0x5c8b38 PointThroughBoneChain
#include "sim/effect_script.h"                // W18-ESC: the .esc VM + command set
#include <cmath>
#include <cstdint>
#include <string>

namespace guild::render {

// ---------------------------------------------------------------------------
// Season smoke-window tables (get_bytes @0x6476FC / @0x64770C, bit-exact).
//   season 0: 08:00..20:00   season 1: 07:00..21:00
//   season 2: 08:00..20:00   season 3: 09:00..19:00
// ---------------------------------------------------------------------------
const float kSmokeStartHour[4] = {8.0f, 7.0f, 8.0f, 9.0f};
const float kSmokeEndHour[4]   = {20.0f, 21.0f, 20.0f, 19.0f};

// The two literal strings the original embeds (0x61ded4 / 0x61dee4).
static const char kDummyRauch0[]      = "dummy_RAUCH_0";
static const char kSchornsteinScript[] = "effekte\\Schornstein_dunkel.esc";

// ===========================================================================
// Inert default hooks — let the headless library link and the golden tests run
// over synthetic memory. Each default reproduces the SHAPE of the original's
// result so the control flow is exercised without engine state:
//   - no smoke type-gate (type byte 0, != 7), no building node (=> early-out is
//     the safe inert path), no live effect slot, an "outside the window" clock.
// The DEFAULT runSmokeScript delegates to W8-EMITTER's SpawnEmitterAtPosition
// (the CreateEmitter the real script body calls), so when a host supplies the
// record/node hooks but no script VM, a real smoke system is still produced at
// the chimney's world position — the faithful observable effect.
// ===========================================================================
namespace {

u8    DefRecordType(void*)                       { return 0; }
u8    DefBuildingTypeStateByte(u8)               { return 0; }
void* DefRecordBuildingNode(void*)               { return nullptr; }
i32   DefRecordSmokeScript(void*)                { return -1; }
void  DefSetRecordSmokeScript(void*, i32)        {}
i32   DefGameTimeDay()                            { return 0; }
i32   DefGameTimeHour()                           { return 0; }
void* DefFindEffectSlot(void*, bool* m)          { if (m) *m = false; return nullptr; }
i32   DefEffectSlotPayload(void*)                { return 0; }
i32   DefSwitchActiveSlot(i32, const char*, i32) { return 0; }
float* DefFindDummyNode(void*, const char*)      { return nullptr; }
void* DefFindScriptByHandle(i32)                 { return nullptr; }
void  DefFinishScript(void*)                     {}
void* DefQueryBuildingsBegin(void*)              { return nullptr; }
void* DefQueryBuildingsNext()                    { return nullptr; }
void  DefFlagBuildingNode(void*)                 {}

// The default smoke-script run: spawn the emitter system the script body would,
// at the dummy's world position. `dummyNode` becomes the system owner (the
// chimney node the smoke rides). Returns a non-negative pseudo-handle on success
// (the original stores scr[+128]); -1 if the emitter could not be created.
i32 DefRunSmokeScript(const char* /*scriptPath*/, void* dummyNode,
                      i32 worldX, i32 worldY, i32 worldZ) {
    // The script's CreateEmitter places at the owner origin and rides the owner's
    // world transform; here we hand the emitter the dummy's resolved world pos so
    // it sits at the chimney even without an owning scene transform.
    const float worldPos[3] = {(float)worldX, (float)worldY, (float)worldZ};
    // kind 1 = polys (the smoke billboard system); texture/slot left to the
    // engine-default path (null => the spawn uses its default texture binding).
    ParticleSystem* sys =
        SpawnEmitterAtPosition(/*kind*/ 1, worldPos, /*owner*/ dummyNode,
                               /*texName*/ nullptr, /*texSlot*/ 0);
    if (!sys)
        return -1;
    // A stable, non-(-1) handle so the caller stores it at record[+149]. We key
    // it off the system pointer (low bits) — opaque to the caller, matching the
    // original's "store the running handle" semantics.
    i32 h = (i32)((std::uintptr_t)sys & 0x7fffffff);
    return (h == -1) ? 0 : h;
}

BuildingFxHooks MakeDefaults() {
    BuildingFxHooks d;
    d.recordType            = DefRecordType;
    d.buildingTypeStateByte = DefBuildingTypeStateByte;
    d.recordBuildingNode    = DefRecordBuildingNode;
    d.recordSmokeScript     = DefRecordSmokeScript;
    d.setRecordSmokeScript  = DefSetRecordSmokeScript;
    d.gameTimeDay           = DefGameTimeDay;
    d.gameTimeHour          = DefGameTimeHour;
    d.findEffectSlot        = DefFindEffectSlot;
    d.effectSlotPayload     = DefEffectSlotPayload;
    d.switchActiveSlot      = DefSwitchActiveSlot;
    d.findDummyNode         = DefFindDummyNode;
    d.runSmokeScript        = DefRunSmokeScript;
    d.findScriptByHandle    = DefFindScriptByHandle;
    d.finishScript          = DefFinishScript;
    d.queryBuildingsBegin   = DefQueryBuildingsBegin;
    d.queryBuildingsNext    = DefQueryBuildingsNext;
    d.flagBuildingNode      = DefFlagBuildingNode;
    return d;
}

BuildingFxHooks  g_defaults = MakeDefaults();
BuildingFxHooks* g_hooks    = &g_defaults;

// frndint with the round-toward-zero / truncation control the original installs
// (VIBE_Coord_ConvertX @0x5c6b08 sets CW high byte = 0x1F = RC=11 truncate).
// Applied to each PointThroughBoneChain output component before the script call.
inline i32 ConvertCoord(float v) {
    return (i32)v;  // C truncation == frndint-truncate for the in-range coords
}

} // namespace

void SetBuildingFxHooks(BuildingFxHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaults;
}
BuildingFxHooks* BuildingFxHooksPtr() { return g_hooks; }

// ===========================================================================
// EFFECT-SCRIPT VM BRIDGE (wave-18, W18-ESC). Run the real effect script through
// sim/effect_script and spawn one live particle SYSTEM per emitter it created.
// ===========================================================================
int SmokeScriptToEmitters(const char* scriptSource, void* owner,
                          i32 worldX, i32 worldY, i32 worldZ) {
    if (!scriptSource)
        return 0;
    bool ok = false;
    // Run the script's main(x,y,z) — the chimney world position. The VM executes
    // the body's CreateEmitter / SetParticlePos / SetEmitter* commands.
    sim::EffectVm vm = sim::RunEffectScriptSource(
        kSchornsteinScript, scriptSource, worldX, worldY, worldZ, &ok);
    if (!ok)
        return 0;
    int spawned = 0;
    for (const sim::EffectEmitter& e : vm.emitters) {
        if (!e.alive)
            continue;
        // The emitter rides the script-placed position (SetParticlePos wrote
        // pos = (x, z, y)); the smoke billboard kind is the CreateEmitter `kind`.
        const float worldPos[3] = {e.posX, e.posY, e.posZ};
        ParticleSystem* sys =
            SpawnEmitterAtPosition((u8)e.kind, worldPos, owner,
                                   /*texName*/ nullptr, e.texSlot);
        if (sys)
            ++spawned;
    }
    return spawned;
}

// The host-installed reader that resolves a script path to its source text.
namespace {
SmokeScriptReader g_smokeReader = nullptr;
void*             g_smokeReaderUser = nullptr;
} // namespace

void SetSmokeScriptReader(SmokeScriptReader reader, void* user) {
    g_smokeReader = reader;
    g_smokeReaderUser = user;
}

i32 RunSmokeScriptViaVm(const char* scriptPath, void* dummyNode,
                        i32 worldX, i32 worldY, i32 worldZ) {
    if (!g_smokeReader)
        return -1;                       // no reader -> behave like a missing file
    std::string source;
    if (!g_smokeReader(scriptPath, source, g_smokeReaderUser) || source.empty())
        return -1;
    int spawned = SmokeScriptToEmitters(source.c_str(), dummyNode,
                                        worldX, worldY, worldZ);
    if (spawned <= 0)
        return -1;
    // A stable, non-(-1) running handle for record[+149] (the original stores
    // scr[+128]); we encode the spawn count in a small positive handle.
    return spawned;
}

// ===========================================================================
// 0x4b60a0 — VIBE_Object_SpawnChimneySmoke. 1:1 control flow.
// ===========================================================================
void SpawnChimneySmoke(void* record, i32 slotArg) {
    BuildingFxHooks* H = g_hooks;
    if (!record)
        return;

    // bl = season = GetSeasonFromDay(&qword_13CE852) == day % 4.       (0x4b60af)
    // HARDEN (wave-10): in real play `day` is non-negative so `day % 4` is in
    // {0,1,2,3} and `& 3` is a no-op — the in-window/in-bounds path stays
    // byte-identical. For a degenerate negative `day`, C++ `%` yields a negative
    // remainder which casts to a large u8 (e.g. -1 -> 255) and would index the
    // 4-entry season tables (kSmokeStartHour/kSmokeEndHour) out of bounds. The
    // `& 3` keeps the index in [0,3] (faithful guard; no observable change for
    // the original's real day>=0 inputs).
    const u8 season = (u8)(H->gameTimeDay() % 4) & 3u;

    // Gate 1: building type-def state byte != 7  AND  building node present.
    //   v.bh = *(typeTable + 589 * record[0]);  if (bh == 7) return;   (0x4b60d9)
    //   if (record[+97] == 0) return;                                  (0x4b60e2)
    const u8 typeIndex = H->recordType(record);
    if (H->buildingTypeStateByte(typeIndex) == 7)
        return;
    void* buildingNode = H->recordBuildingNode(record);
    if (!buildingNode)
        return;

    // The active-record effect-slot scan (0x4b60ee..0x4b626a). `matched` is the
    // original's ecx ("v6") "a row matched" flag; `slot` is dword_12CEA8C[...].
    bool matched = false;
    void* slot   = H->findEffectSlot(record, &matched);

    // 0x4b6250: if a row matched -> matched=true (v6=1). If that slot already has
    // a live payload (slot[+200] != 0), the smoke is up: go straight to the
    // "expire if the script handle died" path (LABEL_23).
    bool gotoExpireCheck = false;
    if (matched && slot && H->effectSlotPayload(slot) != 0) {
        gotoExpireCheck = true;  // -> LABEL_23
    }

    if (!gotoExpireCheck) {
        // LABEL_7 (0x4b6111): decide spawn vs. nothing vs. expire-check.
        //   if (!matched                                   (no slot row)
        //       || record[+149] != -1                      (smoke already running)
        //       || hour <  smokeStartHour[season]          (before the window)
        //       || hour >= smokeEndHour[season])           (after the window)
        //   { if (matched) return;  else goto LABEL_23; }
        const i32  curScript = H->recordSmokeScript(record);
        const float hour     = (float)H->gameTimeHour();   // WORD2(qword_13CE852)
        const bool outOfWindow =
            (hour <  kSmokeStartHour[season]) || (hour >= kSmokeEndHour[season]);

        if (!matched || curScript != -1 || outOfWindow) {
            if (matched)
                return;            // 0x4b629b: a slot exists but conditions not met
            gotoExpireCheck = true; // 0x4b629b: goto LABEL_23
        } else {
            // --- the SPAWN path (0x4b6162..0x4b6214) ---
            // SwitchActiveSlot(0, 1, "dummy_RAUCH_0", slotArg); save prev slot.
            const i32 prevSlot =
                H->switchActiveSlot(0, kDummyRauch0, slotArg);   // 0x4b6175

            // node = Object_FindByHandle(record[+97], 256, ..., slotArg).
            float* node = H->findDummyNode(buildingNode, kDummyRauch0); // 0x4b6184

            if (node) {
                // The script body's CreateEmitter is run via runSmokeScript; the
                // original first loads the .esc (LoadFromScriptDir) and only then
                // computes the position + runs main(z,y,x). We resolve the world
                // position here (PointThroughBoneChain on the dummy) so the run
                // hook receives the exact integer coords the script main() takes.
                float world[3] = {0.0f, 0.0f, 0.0f};
                // PointThroughBoneChain(node, node+19f, world)         (0x4b61b0)
                util::PointThroughBoneChain(node, node + 19, world);
                // var_20 = (int)world[2]; then world[1]; then world[0] — the args
                // are pushed z, y, x (RunWithArgs main(arg0=z, arg1=y, arg2=x)).
                const i32 wz = ConvertCoord(world[2]);
                const i32 wy = ConvertCoord(world[1]);
                const i32 wx = ConvertCoord(world[0]);

                // Script_LoadFromScriptDir + Script_RunWithArgs(scr, 3, z, y, x):
                // returns the running handle (scr[+128]) or -1.        (0x4b61ee)
                const i32 handle =
                    H->runSmokeScript(kSchornsteinScript, node, wx, wy, wz);
                if (handle != -1)
                    H->setRecordSmokeScript(record, handle);  // record[+149]=scr[+128]
                // scr[+132] = 0 (0x4b620a) — reset the script's frame latch. The
                // run hook owns the script object, so this latch reset is internal
                // to runSmokeScript (no separate field to clear in this model).
            }

            // SwitchActiveSlot(prevSlot, 1, ...) — restore.            (0x4b621b)
            H->switchActiveSlot(prevSlot, kSchornsteinScript, 0);
            return;
        }
    }

    // LABEL_23 (0x4b6270): the EXPIRE check. If a smoke script handle is recorded
    // but its script has finished/expired, drop the handle (and finish it if it
    // somehow still resolves).
    if (gotoExpireCheck) {
        const i32 handle = H->recordSmokeScript(record);     // 0x4b6270
        if (handle != -1) {                                  // 0x4b6276
            void* sc = H->findScriptByHandle(handle);        // 0x4b627d
            if (sc)
                H->finishScript(sc);                         // 0x4b62a6
            H->setRecordSmokeScript(record, -1);             // record[+149] = -1
        }
    }
}

// ===========================================================================
// 0x504910 (smoke arm) — VIBE_Scene_RefreshBuildingEffects.
//   for (i = Person_QueryBegin(world, 1, 6); i; i = Person_IterNext()) {
//     node = i[+97];                                        (0x504923)
//     if (node) {
//       node[+530] = (node[+530] & 0xF3) | 4;               (0x504930..0x50493e)
//       SpawnChimneySmoke(i, slotArg);                      (0x504944)
//     }
//   }
// (The original then rebuilds lights/octree/terrain; those are other modules.)
// ===========================================================================
BuildingSmokeAttachResult AttachCityBuildingSmoke(void* world, i32 slotArg) {
    BuildingFxHooks* H = g_hooks;
    BuildingSmokeAttachResult R;

    for (void* rec = H->queryBuildingsBegin(world); rec;
         rec = H->queryBuildingsNext()) {
        void* node = H->recordBuildingNode(rec);             // i[+97]
        if (!node)
            continue;
        H->flagBuildingNode(node);                           // node[+530] = (b&0xF3)|4
        const i32 before = H->recordSmokeScript(rec);
        SpawnChimneySmoke(rec, slotArg);
        ++R.buildings;
        const i32 after = H->recordSmokeScript(rec);
        if (before == -1 && after != -1)
            ++R.smokeSpawned;
    }
    return R;
}

} // namespace guild::render
