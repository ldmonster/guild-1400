# building-fx — Wave-8 (W8-SMOKE): building chimney smoke attach

Module: `src/render/building_fx.{h,cpp}` (namespace `guild::render`)
Tests: `tests/unit/building_fx_test.cpp` (suite `BuildingFx`, 14 tests / 51 checks,
0 failures)

The rising smoke over a building's chimney / forge / kitchen. In gilde.exe this is
NOT hand-built C++ geometry — it is a particle emitter SYSTEM produced by RUNNING
the effect script `effekte\Schornstein_dunkel.esc` at the world position of the
building's `dummy_RAUCH_0` ("RAUCH" = German "smoke") dummy node. The script body
calls the `CreateEmitter` script command (0x43fd24), which spawns the particle
system the per-frame render walk draws (W6/W7 + W8-EMITTER own that pipeline).

## Reconstructed functions (gilde.exe, imagebase 0x400000)

| addr | symbol | reconstruction |
|---|---|---|
| 0x4b60a0 | `VIBE_Object_SpawnChimneySmoke(a1@eax, a2@edi)` | `render::SpawnChimneySmoke` — FULL 1:1 control flow (157 instructions). Per-building smoke setup: season gate, type gate, building-node gate, the 768×536 effect-slot scan, the time-of-day window gate, the `dummy_RAUCH_0` lookup, the dummy world-position compute + frndint-truncate, the `Schornstein_dunkel.esc` load+run, and the expire-when-dead path. |
| 0x504910 | `VIBE_Scene_RefreshBuildingEffects` (smoke arm) | `render::AttachCityBuildingSmoke` — the CITY driver: `Person_QueryBegin(world,1,6)` / `IterNext` over every owned building, flag its model node (+530 `= (b&0xF3)|4`), call `SpawnChimneySmoke`. (The original then rebuilds lights/octree/terrain — those are other modules; this entry owns the smoke arm, sequenced first exactly as the binary does.) |

## The EXACT data flow (recovered from disasm @0x4b60a0)

`a1` (the record) is a **building/person entity** from the unified 536-stride
entity array (the `Person_QueryBegin(...,1,6)` result; +97 holds the building model
scene-node handle, +149 the running smoke-script handle). Verified via caller
`VIBE_Command_ExActivateObject @0x49bcf0` (gates `[eax+61h]` and
`Building_IsProductionType` before the call).

```
season = GameTime.day % 4                          ; 0x58339c, qword_13CE852 dword[0]
if (*(byte*)(dword_13CE294 + 589*record[0]) == 7)  return;   ; type 7 == no smoke
if (record[+97] == 0)                              return;   ; no building node
slot = scan(768×536 active-record array):                    ; 0x4b60ee..0x4b626a
        alive && handle!=0xFFFF && kind!=10 &&
        homeBld(+364)==record && slotKind(+384)==4
        -> matched=1, slot = dword_12CEA8C[row]
if (matched && slot && slot[+200]!=0)              goto EXPIRE;  ; smoke already up
; --- LABEL_7 ---
if (!matched || record[+149]!=-1
    || hour <  smokeStartHour[season]
    || hour >= smokeEndHour[season]) {
      if (matched) return; else goto EXPIRE;
}
; --- SPAWN ---
prev = SwitchActiveSlot(0, 1, "dummy_RAUCH_0", a2);          ; 0x5b4a24
node = Object_FindByHandle(record[+97], 256, .., a2);        ; 0x5b7be4
if (node) {
  scr = Script_LoadFromScriptDir("effekte\\Schornstein_dunkel.esc"); ; 0x4424e0
  if (scr) {
    PointThroughBoneChain(node, node+19f, world);            ; 0x5c8b38
    wz=(int)world[2]; wy=(int)world[1]; wx=(int)world[0];    ; 0x5c6b08 frndint trunc
    if (Script_RunWithArgs(scr, 3, wz, wy, wx))              ; 0x443a90 main(z,y,x)
        record[+149] = scr[+128];                            ; store running handle
    scr[+132] = 0;
  }
}
SwitchActiveSlot(prev, 1, ..);                               ; restore slot
return;
; --- EXPIRE (LABEL_23, 0x4b6270) ---
h = record[+149];
if (h != -1) {
  sc = Script_FindByHandle(h);                               ; 0x442174
  if (sc) Script_Finish(sc);                                 ; 0x443f38
  record[+149] = -1;
}
```

KEY semantic (verified from the jump structure): **a matching effect-slot row is a
PREREQUISITE for spawning**. No matching row → expire path (no spawn). Matched +
`slot[+200]==0` → spawn (if in-window & no script running). Matched + `slot[+200]!=0`
→ smoke already up → expire-if-dead.

## Constants recovered by get_bytes (bit-exact)

| symbol | addr | value |
|---|---|---|
| `smokeStartHour[4]` (`kSmokeStartHour`) | 0x6476FC | `{8.0, 7.0, 8.0, 9.0}` |
| `smokeEndHour[4]` (`kSmokeEndHour`)   | 0x64770C | `{20.0, 21.0, 20.0, 19.0}` |
| `aDummyRauch0`        | 0x61ded4 | `"dummy_RAUCH_0"` |
| `aEffekteSchorns`     | 0x61dee4 | `"effekte\\Schornstein_dunkel.esc"` |

Season window meaning: spring/summer/autumn/winter chimneys smoke
08–20 / 07–21 / 08–20 / 09–19. (Cross-validated: `scene_recon2_orchestrator.cpp`
`SmokeTimeGate` already carries the same four-entry tables independently.)

## Which buildings get smoke

Every owned building visited by `Person_QueryBegin(world, 1, 6)` whose building-type
state byte (`*(dword_13CE294 + 589*type)`) is **not 7** and which has a model node
(+97) AND a registered effect-slot row (slotKind 4). Type-7 buildings never smoke.
(Note: callers other than the city driver — `ExActivateObject`/`ExGebUpgrade`/
`RunMainFrameLoop` — additionally pre-gate on `Building_IsProductionType` = type ∈
{11,12,13,16,28}, but the leaf itself only excludes type 7.)

## Reused (not redefined) — grep-checked, no ODR clash

- `util::PointThroughBoneChain` (`src/util/transform.h`, 0x5c8b38) — the dummy
  world-position math.
- `render::SpawnEmitterAtPosition` (`src/render/particle_emitter_create.{h,cpp}`,
  W8-EMITTER) — the `CreateEmitter`/AllocSystem equivalent the script body invokes;
  the default `runSmokeScript` hook spawns exactly that system at the dummy's world
  position, so a real smoke system is produced even without the script VM.
- `render::kSmokeStartHour/kSmokeEndHour` are new in `guild::render`; the existing
  `scene_const::kSmokeStartHour` (play/scene_recon2_orchestrator.h) is a different
  namespace — no clash.

## THE HANDOFF (rule 13 — what the orchestrator wires)

`SpawnChimneySmoke` is already declared as a NAMED GAP / hook stub in several
in-tree call sites awaiting this reconstruction:
- `play/scene_recon2_orchestrator.{h,cpp}` — `SceneHooks::ObjectSpawnChimneySmoke`
  (0x4b60a0); the `RunMainFrameLoop` smoke phase-machine already calls it per
  in-window frame. **Wire: point `ObjectSpawnChimneySmoke` at a thunk that calls
  `render::SpawnChimneySmoke(person, slotArg)`.**
- `play/scene_main_loop.{h,cpp}` — `objectSpawnChimneySmoke(person)` virtual.
- `sim/cutscene_misc5.h` — `spawnChimneySmoke(person, a)` hook.
- `sim/command_apply4.h` — documents 0x4b60a0 for the production-building command path.

City/session load handoff (the `RefreshBuildingEffects` caller chain):
`VIBE_GameLogic_InitOrLoadSession @0x533a54` / `VIBE_Save_LoadGameFile @0x5a7604`
→ `VIBE_Scene_RefreshBuildingEffects @0x504910` → per-building `SpawnChimneySmoke`.
**Wire: after a city/save loads, call
`render::AttachCityBuildingSmoke(world, slotArg)`** with a `BuildingFxHooks` set
bound to the live entity array / scene graph / script VM. The render side
(`particle_render` / `fx_recon3` walked by `BeginUniverseFrame`, integrated by the
W6/W7/W8-EMITTER cluster) then draws every spawned system each frame — no extra
wiring; smoke systems are ordinary entries in the live particle-system list.

The boundary is the `BuildingFxHooks` vtable (see header): record/type-table reads,
the clock (GameTime.day / .hour), the effect-slot scan, `SwitchActiveSlot`,
`Object_FindByHandle` (dummy lookup), the script VM (load/run/find/finish), and the
city iterator + node flag. The headless library links with fully inert defaults
(the default `runSmokeScript` still spawns a real emitter via W8-EMITTER); hosts
install live hooks.

## Named gaps (rule 8 — addresses, why deferred)

- **The `.esc` script VM** (`Script_LoadFromScriptDir 0x4424e0`,
  `Script_RunWithArgs 0x443a90`, `Script_FindByHandle 0x442174`,
  `Script_Finish 0x443f38`): the engine's interpreter for `Schornstein_dunkel.esc`.
  Reaching it 1:1 means reconstructing the whole scripting subsystem (compiler +
  VM); out of this module's scope. Modeled as the `runSmokeScript`/`findScriptByHandle`/
  `finishScript` hooks. The DEFAULT `runSmokeScript` produces the faithful observable
  output (a smoke emitter at the chimney world pos) via `SpawnEmitterAtPosition`,
  so the effect is real even before the VM lands.
- **`VIBE_Universe_SwitchActiveSlot 0x5b4a24`**: the 246-stride universe-slot
  context switch (camera/light/particle-list save+restore). Already a known engine
  plumbing hook elsewhere in the tree (building-scene-attach). Modeled as the
  `switchActiveSlot` hook (returns the previous slot for restore).
- **`VIBE_Object_FindByHandle 0x5b7be4`** (dummy lookup): walks the scene graph via
  `SceneGraph_WalkAndInvoke` + `MatchHandleCallback`. The walk is engine-scene-graph
  memory; modeled as the `findDummyNode` hook (the building-scene-attach module owns
  the real scene tree).
- **The 768×536 effect-slot scan** (0x4b60ee): the `dword_12CEA8C`/`byte_12CEA90`
  active-record columns are the live entity array the host owns; modeled as the
  `findEffectSlot` hook (the scan predicate is documented above, byte-exact).

## Tests (`BuildingFx`, 14 / 51 checks, 0 failures)

SeasonWindowTables (the bit-exact @0x6476FC/@0x64770C tables),
SpawnsAtDummyWorldPosition (PointThroughBoneChain handoff: world (100,250,400) →
args z=400,y=250,x=100, handle stored), Type7NoSmoke, NoBuildingNodeNoSmoke,
OutsideWindowNoSmoke (before 8:00 / after 20:00), Season3Window (9–19 window; day%4
indexing), AlreadyRunningNoRespawn, ExpireDeadScript (handle dropped to -1),
ExpireLiveScriptFinishes (Script_Finish + clear), MissingDummyNoRun (slot pushed/
restored, no run), RunFailKeepsHandleClear, CityDriverAttachesAll (3 buildings
flagged + smoked), CityDriverSkipsNodeless, InertDefaultsSafe.
