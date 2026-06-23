# Character-from-model factory trio

The single path through which EVERY character record is created — dynasty/menu
actors (`menu_actor.cpp`'s `CreateMenuDummyActor`), live in-game NPCs
(`personnel`/`recruit` spawns), scripted actors (`script_import3.cpp`'s
`HostCreateFromModel`) and animals (`animal.cpp`) all bottom out here.

Module: `src/sim/character_factory.{h,cpp}`. Tests: `tests/unit/character_factory_test.cpp`.

## Reconstructed (1:1)

- `VIBE_Character_AllocSlot @0x402254` — **REUSED**, not redefined. Already
  reconstructed in `src/sim/character_path.cpp` as `guild::sim::AllocSlot()`
  (fills the real 512-entry `g_live` table from `character_query.cpp`, writes the
  slot index back to `record[0]`). `CreateFromModel` calls it.
- `VIBE_Character_CreateMesh @0x4029c4` — the factory body. Loads the model
  (`Object_AttachToUniverseNode`), decomposes the name into base/prefix, writes
  the initial record fields and the object-node flag bytes, runs the creature-type
  probe, and preloads the gait+idle anim set (and the low-poly set when enabled).
- `VIBE_Character_CreateFromModel @0x402d10` — the trivial wrapper (AllocSlot →
  CreateMesh → on failure Destroy + return null).

## Recovered details (reference of record)

- **Name decomposition** (disasm @0x402a4c): `p = strchr(name,'_')`; if found,
  `q = strchr(p+1,'_'); if (q) *q=0;` then base (`rec+304`) = substring after the
  first `_` (truncated at the second `_`), and prefix (`rec+368`) = everything
  before the first `_`. No `_` → base = `byte_610134` (an **empty string**), prefix
  field untouched. So `dieb_MANN2` → base `MANN2`, prefix `dieb`;
  `dieb_MANN2_alt` → base `MANN2` (tail dropped), prefix `dieb`; `MANN2` → base ``.
- **Record writes**: +0 slot index (AllocSlot), +4 type byte (1 human / 2 animal,
  `|8` horse), +5 full model name, +40 script handle = -1, +44/+48 = -1, +52 node,
  +136 universe (`off_649D64`), +141 `|= 0x10`, +304 base, +368 prefix,
  +416 scale `1.0f` (`0x3F800000`), +492 low-poly gate.
- **Object-node flags**: +530 `|= 0x0C`, +529 `&= ~2` then `|= 4`, +535 = 2,
  +536 = 1, +531 `&= ~4`, +72 = `0x3000000`, `*(node+492)+2296` = `1.8f`
  (`0x3FE66666`). Local-universe vs network: `IndexFromPointer(universe)` truthy →
  +529 `&= ~8`, else fold `(dword_62D010&1)<<3` into +529.
- **Creature probe** (`loc_5CB930` is `strstr` on `rec+5`): `RATTE/HUND/KATZE/PFERD`
  → type 2; `RATTE` also clears node +529 bits 8 & 4; `PFERD` also sets type `|8`
  and clears node +529 bit 8.
- **Constants**: `dword_401010[0..3] = {0,0,0,0}` (attach transform seed).

## Named hooks / gaps (rule 8 — no fakes)

Routed through `CharacterFactoryHooks` (inert defaults):
- `Object_AttachToUniverseNode @0x5b3e30` — .bgf model load + scene-node attach.
  **Now WIRED (rule 13):** the genuine 1:1 reconstruction lives in
  `sim/object_lifecycle10.{h,cpp}` as `guild::sim::ObjectAttachToUniverseNode`
  (spawn type-4 → SetParent → Mesh find-or-load → AttachStockObjectLods → the
  nested LOD×texture upload loop → SetPosition/SetWorldTranslation → root-only
  LinkIntoScene; mesh-load-fail → Dispose → null). `sim/object_attach_wiring.{h,cpp}`
  (`InstallRealObjectAttachWiring`, called from `app/wiring.cpp` alongside the
  `InstallRealSimHooks*` pass) binds the factory's `attachToUniverseNode` slot to it
  with the exact live-call mapping (parent=0, pos=`parentMat` edx, xlate=`dword_401010`
  {0,0,0,0}, ctx=`&model`). It is NOT redefined (ODR). The universe leaves BELOW it
  (`Object_Spawn`/`SetParent`/`Mesh_*`/`Texture_UploadToSurface`/`SetPosition`/
  `SetWorldTranslation`/`LinkIntoScene`/`Dispose`) stay inert in `ObjLife10Hooks`
  (the .bgf stock cache + Vulkan texture upload + scene list are deferred targets).
  Golden coverage of the loop core (incl. the LP64 +260/+264 adjacent-pointer
  adaptation) is in `tests/unit/attach_universe_node_test.cpp` (suite
  `AttachUniverseNode`, **7 tests, 47 checks, 0 failures**).
- `IndexFromPointer @0x426724` — universe ptr → slot index (reconstructed-exact
  semantics documented; default inert returns 0).
- `QueryTerrainType @0x404650`, `BuildObjectCache @0x5c8218`,
  `PropagateDirtyFlag @0x5af2c0`, `UpdateLowPolyMesh @0x40244c`.
- `PreloadAniSet @0x403c34` / `PreloadLowPolyAniSet @0x403da0` — full
  reconstructions live in `character_render3.cpp`; reached via hook so the test can
  record the exact clip names (`bewegung/gehen`, `stehen/stehen_newnoise`, `gehen`).
- `loc_5CB930` — strstr; the default uses real `std::strstr`.
- `Destroy @0x402120` — the large teardown (action-queue clear, mesh/object-node
  release, morph teardown, universe-slot switch). Modeled as a hook (its own
  reconstruction target); the inert default drops the `g_live` slot + frees.

## Tests

`tests/unit/character_factory_test.cpp` (suite `CharacterFactory`, **12 tests,
66 checks, 0 failures**): AllocSlot first-free fill + record[0]; full-table → null;
name decomposition (one-`_`, two-`_`, no-`_` default); CreateMesh name fields,
success fields/flags + preload clip names, creature probe (horse type `2|8`) +
low-poly preload, model-load failure (null node → 0 + error), null record → 0;
CreateFromModel success returns the slot, failure destroys + returns null.

## Wiring (rule 13)

`CreateFromModel` calls the reused `AllocSlot()` and the new `CreateMesh`. The
scene-graph leaves are hooks; the live game installs real wiring (the menu-actor /
recruit / script-import callers already model `Character_CreateFromModel` through
their own hook slots and can now bind to `guild::sim::CreateFromModel`).
